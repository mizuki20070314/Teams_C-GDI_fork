# Teams C+GDI (preview)

WebViewを使わない、C / Win32 / GDIベースの軽量クライアント試作です。画面と操作の土台を先に作り、現在はローカルのサンプル会話で動作します。

## ビルド

Windows上でCMakeとCコンパイラを用意し、リポジトリのルートで実行します。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

生成物は `build/TeamsCGDI.exe` です。

## 操作

- 左のナビゲーションでアクティビティ、チャット、チーム、予定表、通話、ファイルを切り替え
- チャット一覧を選ぶと会話を表示
- 入力欄で Enter を押すとローカル会話に送信、Shift+Enter で改行
- `Ctrl+N` でローカルの新規会話を作成
- 上部検索はチャット一覧を絞り込み

## 現在の制限

- Microsoft Teamsへのログインや実メッセージ送受信は未実装です。現在の会話はデモデータで、送信内容は `%LOCALAPPDATA%\TeamsCGDI\messages.tsv` に保存します。
- 実Teamsとの連携には、Microsoft Graphのアプリ登録、OAuth認証、および利用者に許可されたAPI権限が必要です。通話・会議のリアルタイム連携も別途設計が必要です。
- UIはGDIで独自描画しており、Teamsの画像・アイコン・コードは同梱していません。
