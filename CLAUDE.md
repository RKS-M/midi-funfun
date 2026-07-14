## モデル選定ルール
新しいタスクを受けたら、以下に照らして適切な階層を判定し、
該当するサブエージェントに委任すること。判定結果は一言で報告する。

- haiku-worker: 単純な質問、フォーマット整形、リネーム、typo修正、
  ファイルの中身を読むだけの調査
- sonnet-worker: 通常の実装・バグ修正・テスト作成・一般的なコーディング作業(デフォルト)
- opus-reviewer: 設計判断、複雑なバグの根本原因調査、アーキテクチャレビュー、
  セキュリティ関連の分析

迷ったら一段階上の階層を選ぶこと。

**委任時の実装上の注意(重要):** この環境の `Agent` ツールは `.claude/agents/*.md`
のカスタムエージェント名(`haiku-worker`/`sonnet-worker`/`opus-reviewer`)を
`subagent_type` として直接認識しない(組み込みの固定一覧
`claude`/`claude-code-guide`/`Explore`/`general-purpose`/`Plan`/`statusline-setup`
以外はエラーになる)。実際に委任する際は、必ず以下の形で呼び出すこと:

- `subagent_type: "claude"` を指定する
- `model` パラメータに階層に応じたモデル(`haiku`/`sonnet`/`opus`)を明示指定する
- 該当する `.claude/agents/<tier>.md` の本文(ペルソナ・責務・ガイドライン)を
  プロンプト冒頭に埋め込む

`subagent_type` にそのまま `"haiku-worker"` 等を渡す呼び出しは失敗するので行わないこと。
