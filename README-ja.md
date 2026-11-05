# juce-webgpu

[English version](./README.md)

JUCEの`Component`にWebGPUを組み込むための小さなCMakeライブラリです。
描画バックエンドには[wgpu-native](https://github.com/gfx-rs/wgpu-native)を使用します。

## 特徴

- `webgpu::Context`を使ってJUCEコンポーネントに描画できます。
- `webgpu::Renderer`のコールバックはJUCEのメッセージスレッドから呼び出されます。
- ネイティブサーフェス（Windowsでは子`HWND`、macOSでは`CAMetalLayer`）と、
  JUCEの`Image`へ読み戻すオフスクリーン描画を選択できます。
- `automatic`では、JUCEの子コンポーネントを描画する場合は`image`、
  それ以外では`nativeSurface`が優先されます。
- `webgpu::Device`は`juce::SharedResourcePointer`を通してプロセス内で共有されます。
- wgpu-nativeはCMakeの`FetchContent`で取得して静的リンクするため、
  プラグインにDLLやdylibを同梱する必要はありません。

## 必要環境

- CMake 3.28以上
- C++20以上
- JUCE（9.0.0で確認）。ネイティブサーフェスは`juce::HWNDComponent`（Windows）と
  `juce::NSViewComponent`（macOS）を使うため、利用側のターゲットで
  `juce::juce_gui_extra`をリンクする必要があります。

`juce_webgpu`は`INTERFACE`ライブラリです。ソースは利用側ターゲットの一部として、
そのターゲットのJUCE設定と定義でコンパイルされます。
このライブラリ自身はJUCEをリンクしないため、JUCEは利用側でリンクしてください。
JUCEモジュールをリンクする`STATIC`ライブラリにはしないでください。
JUCEモジュールのソースが二重にコンパイルされます。

## 対応環境

| プラットフォーム | ネイティブサーフェス | アーキテクチャ | 状況 |
| --- | --- | --- | --- |
| Windows | 子`HWND` | x86_64, ARM64 | x86_64で動作確認済み、ARM64は未検証 |
| macOS | `CAMetalLayer` | x86_64, arm64 | ユニバーサルビルドは確認済み、実行は未検証 |
| Linux | なし（`image`のみ） | x86_64, aarch64 | 実験的、未検証 |

Linuxでは`NativeSurfaceView::create()`が`nullptr`を返すため、
`automatic`は`image`方式にフォールバックします。

## 導入

### CMake

`FetchContent`でこのリポジトリを追加する例です。
ビルドを再現できるよう、`GIT_TAG`はブランチではなくリリースタグかコミットハッシュで
固定してください（wgpu-nativeのC APIはメジャーリリースごとに変わります）。

```cmake
include(FetchContent)

FetchContent_Declare(
    juce_webgpu
    GIT_REPOSITORY https://github.com/wheatBread1306/juce-webgpu.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce_webgpu)

target_link_libraries(MyPlugin
    PRIVATE
        juce::juce_gui_extra
        juce_webgpu::juce_webgpu
)
```

macOSで`.mm`をビルドするため、利用側のトップレベルCMakeプロジェクトで
Objective-C++を有効にしてください。

```cmake
if(APPLE)
    enable_language(OBJCXX)
endif()
```

使用するwgpu-nativeのリリースは次のように変更できます。

```sh
cmake -DJUCE_WEBGPU_WGPU_TAG=v29.0.1.1 ...
```

### ローカルでの開発

`FetchContent`の設定のまま手元のチェックアウトを使うには、
ダウンロードの代わりにそのディレクトリを指定します。

```sh
cmake -DFETCHCONTENT_SOURCE_DIR_JUCE_WEBGPU=path/to/juce-webgpu ...
```

サブディレクトリとして追加することもできます。

```cmake
add_subdirectory(path/to/juce-webgpu)

target_link_libraries(MyPlugin
    PRIVATE
        juce::juce_gui_extra
        juce_webgpu::juce_webgpu
)
```

### インクルード

```cpp
#include <WebGPU/WebGPUContext.h>
```

## 最小使用例

`webgpu::Renderer`を実装し、`webgpu::Context`に設定して、
表示可能なJUCEコンポーネントにアタッチします。
`webgpuContextCreated()`でデバイス依存のリソースを作成し、
`renderWebGPU()`で`frame.encoder`を使って`frame.target`に描画し、
`webgpuContextClosing()`でリソースを解放します。

次の例は三角形を1つ描画します。

```cpp
class MyRenderer final : public webgpu::Renderer
{
public:
    void webgpuContextCreated(webgpu::Device& device,
                              WGPUTextureFormat format) override
    {
        const auto module = device.createShaderModule(shaderSource, "Triangle");

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = format;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module.get();
        fragment.entryPoint = webgpu::toStringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.vertex.module = module.get();
        desc.vertex.entryPoint = webgpu::toStringView("vs_main");
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.fragment = &fragment;

        pipeline.reset(wgpuDeviceCreateRenderPipeline(device.getDevice(), &desc));
    }

    void renderWebGPU(const webgpu::Frame& frame) override
    {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = frame.target;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {0.08, 0.10, 0.14, 1.0};

        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &color;

        const webgpu::RenderPassEncoder pass(
            wgpuCommandEncoderBeginRenderPass(frame.encoder, &desc));

        if (pipeline)
        {
            wgpuRenderPassEncoderSetPipeline(pass.get(), pipeline.get());
            wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass.get());
    }

    void webgpuContextClosing() override
    {
        pipeline.reset();
    }

private:
    webgpu::RenderPipeline pipeline;

    static constexpr std::string_view shaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(
        vec2f(0.0, 0.6),
        vec2f(-0.6, -0.6),
        vec2f(0.6, -0.6)
    );
    return vec4f(positions[index], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.55, 0.2, 1.0);
}
)";
};

class MyComponent final : public juce::Component
{
public:
    MyComponent()
    {
        context.setRenderer(&renderer);
        context.setPresentation(webgpu::Context::Presentation::automatic);
        context.attachTo(*this);
    }

    ~MyComponent() override
    {
        context.detach();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
        context.paintImage(g, getLocalBounds().toFloat());
    }

private:
    MyRenderer renderer;
    webgpu::Context context;
};
```

WGSLやパイプラインの検証エラーを検出するには、リソース作成を
`device.pushErrorScope()` / `device.popErrorScope()`で囲んでください。
不正なシェーダーでもnullではないハンドルが返り、エラーはスコープに報告されます。

`image`方式では、コンポーネントの`paint()`から`paintImage()`を呼び出します。
ネイティブ方式では子ビューへ直接描画するため、同じ領域のJUCE子コンポーネントは
その下に隠れます。

対象が表示・非表示になると、コンテキストは自動的に描画を開始・停止します。
対象または`Renderer`を破棄する前に`context.detach()`を呼び出してください。

## 主な設定

```cpp
context.setPresentation(webgpu::Context::Presentation::nativeSurface);
context.setComponentPaintingEnabled(true);
context.setContinuousRepainting(false);
context.setMaximumFrameRate(60.0);
context.setRenderScale(0.5f);
context.triggerRepaint();
```

- `setPresentation()`には`automatic`、`nativeSurface`、`image`を指定します。
- `setComponentPaintingEnabled(true)`にすると、`automatic`は`image`を選択します。
- `setContinuousRepainting(false)`では、`triggerRepaint()`の後だけ描画します。
- `setMaximumFrameRate(0.0)`は無制限です。`setRenderScale()`は`0.05`から
  `4.0`の範囲にクランプされます。
- コンテキストは対象が表示中で、サイズが正かつ最小化されていない場合だけ有効です。
- `Context`と`Renderer`はJUCEのメッセージスレッドから使用してください。

## 注意事項

- `Frame`の`encoder`や`target`を含むハンドルは、`renderWebGPU()`の呼び出し中だけ有効です。
- `renderWebGPU()`では`WGPULoadOp_Clear`を使って描画先全体をクリアしてください。
- `Frame::scale`は論理ピクセルに対する物理ピクセル倍率です。
- アダプターは`WGPUPowerPreference_LowPower`で要求するため、
  内蔵GPUと外部GPUの両方がある環境では通常、内蔵GPUが使われます。
- `webgpu::Device`を作成できない場合、描画は行われず、呼び出し側の背景が残ります。
- ネイティブサーフェスを利用できない場合、`image`方式へフォールバックします。
- `image`方式はGPUからCPUへの読み戻しを行うため、ネイティブ方式より負荷が高く、
  通常は1フレームの遅延があります。

## テストとフィードバック

Windows、macOS、Linuxでの動作確認やフィードバックを歓迎します。
Issueを報告する際は、OS、CPU/GPU（複数GPU環境ではどちらが使われたか）、
JUCEのバージョン、ビルド環境、再現手順、エラーメッセージを可能な範囲で記載してください。

## ライセンス

このプロジェクト独自のソースコードはMIT Licenseで提供されます。
詳細は[LICENSE](./LICENSE)を参照してください。

JUCEは本プロジェクトによって再ライセンスされません。
JUCEはAGPLv3またはJUCE Licenceで提供されています。
JUCEを使用する製品は、利用するJUCEのライセンス条件に従ってください。

wgpu-nativeおよびその他の外部依存関係には、それぞれのライセンス条件が適用されます。
再配布時は各依存関係のライセンスも確認してください。
