# Audio-Bridge

<img width="319" height="330" alt="スクリーンショット 2026-08-24 004649" src="https://github.com/user-attachments/assets/0fdaf94d-5f41-4e22-a824-215510e3a812" />


Audio-Bridgeは、DAWやVSTホスト上から直接音声をWindowsの音声デバイスへ出力出来るVSTプラグインです。ホストの通常入出力に加え、MME、DirectSound、WASAPIのデバイスを選択できます。

## 主な機能

- 入力／出力ゲイン：-30～+30 dB
- 出力ディレイ：0～2000 ms
- MME、DirectSound、WASAPI共有／排他モード
- ホストと外部デバイス間のクロック差補正
- ゲイン、ディレイ、デバイス選択などをVST3状態として保存

外部デバイスが未選択のときは、通常のVSTと同じくホスト入力からホスト出力へ音声を通します。外部入力または外部出力を選ぶと、該当側は外部デバイス経路へ切り替わり、ホスト経路とは混ざりません。

ASIOドライバを直接開く機能はありません。ASIOを使用するDAW／VSTホスト内では、ホストの通常入出力経路としてASIO音声を扱えます。

設定はレジストリや独自設定ファイルへ保存せず、DAW／VSTホストのプロジェクトまたはプリセット保存に従います。

## 対応環境

- Windows x64
- VST3対応DAW／VSTホスト

## インストール

ReleaseのZIPを展開し、`Audio-Bridge.vst3`をVST3プラグインフォルダへコピーしてください。

## ビルド

Visual Studio 2022 C++ツールとCMake 3.25以降が必要です。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

初回構成時に固定バージョンのVST3 SDKを自動取得します。

## ライセンス

Audio-Bridgeは[MIT License](LICENSE)で公開します。依存ライブラリの表記は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。
