[日本語](README.md) | [English](README_EN.md)

# Audio-Bridge

現行リリース: `0.4.2`

<img width="319" height="330" alt="スクリーンショット 2026-08-24 004649" src="https://github.com/user-attachments/assets/0fdaf94d-5f41-4e22-a824-215510e3a812" />


Audio-Bridgeは、DAWやVSTホスト上から直接音声をWindowsの音声デバイスへ入出力できるVSTプラグインです。ホストの通常入出力に加え、MME、DirectSound、WASAPIのデバイスと、再生中のアプリケーション単位の音声入力を選択できます。

## 主な機能

- 入力／出力ゲイン：-30～+30 dB
- 出力ディレイ：0～2000 ms
- Application Inputによるアプリケーション単位の音声入力
- MME、DirectSound、WASAPI共有／排他モード
- ホストと外部デバイス間のクロック差補正
- ゲイン、ディレイ、デバイス選択などをVST3状態として保存

ホスト入力は常にホスト出力へ通ります。ADDITIONAL INPUTを選ぶと、その音を入力ゲイン適用後にホストの音へ加算します。ADDITIONAL OUTPUTを選ぶと、合流した音を後段へ通しながら外部にも送ります。入力ゲインは追加入力がOFFならDAW入力、選択中なら外部入力だけに作用します。出力ゲインと出力ディレイは追加出力がOFFならDAW出力、選択中なら外部出力だけに作用します。外部出力選択中もDAW出力には合流した音がゲイン・遅延なしで流れます。未選択（OFF）は追加経路を無効にします。ホスト入力が無ければ外部入力だけが流れます。

0.3.1のVST設定は読み込めますが、外部入力は置き換えから加算へ、外部出力は切替から分岐へ変わります。同じ音の重複、音量超過、入力と出力の循環、経路間の時間差は接続とゲインで調整してください。自動正規化や自動的な遅延合わせは行いません。

ADDITIONAL INPUTの`[Application Input]`から、Windowsの再生音声セッションを持つアプリケーションを選ぶと、そのアプリケーションのプロセスツリーだけを入力できます。これはシステム全体を取り込む旧来のシステムループバックではなく、選択したアプリケーション単位の入力です。アプリケーション一覧は`REFRESH`で更新できます。アプリケーションを起動してから音声が再生されるまで一覧に現れない場合があります。選択状態として保存するのは実行ファイルの識別子のみで、プロセスIDは起動ごとに解決します。

ASIOドライバを直接開く機能はありません。ASIOを使用するDAW／VSTホスト内では、ホストの通常入出力経路としてASIO音声を扱えます。

設定はレジストリや独自設定ファイルへ保存せず、DAW／VSTホストのプロジェクトまたはプリセット保存に従います。

## 対応環境

- Application Input使用時はWindows 10 Version 2004以降／Windows 11 x64
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
