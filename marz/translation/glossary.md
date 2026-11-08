# Virtual-On Marz English translation: glossary and style guide

Goal: a professional, faithful English translation of the Japanese text of Dennou Senki Virtual-On Marz
(Dramatic Mode story narration, briefings, character dialogue, database, battle chatter, tutorial). Keep the
meaning, tone and detail of the original Japanese prose; do not summarise, simplify or invent. The old official
English release cut and changed the story because it could not fit; this translation must not. Where a line limit
forces economy, choose tighter English wording, never drop content.

## Format rules (checked by tools/text_check.py)
- Plain ASCII is preferred. Also allowed and drawn narrow: “ ” ‘ ’ (curly quotes), — (em dash), ‐.
  Do not use backslash. Write "..." for an ellipsis (… is converted to ... automatically).
- Other Japanese symbols are allowed but draw full width (wide): ♪ ○ × □ △ ■ ★ ● ◆ ＜ ＞ → etc. Keep ♪ where a
  character uses it. Controller buttons: write "○ button", "× button", "□ button", "△ button" (the symbols exist in
  the font), and L1/R1/START as plain ASCII.
- msd units: the translation is a JSON list with exactly as many lines as the Japanese (each line is one display
  line of a page; the page shows them together). Every line non-empty, at most 48 bytes. Break lines where English
  reads naturally across the page; the content of the page as a whole matters, not line-by-line correspondence.
- db units: one string, lines separated by \n. Lines at most 50 bytes. Keep the layout: blank lines, section
  headers like "■ Unit Name ■", indents (use leading spaces), lists. Do NOT include the first line of the file (the
  line count); the tool writes it. Keep the file within its byte room (the checker tells you); English may use more
  lines than the Japanese.
- elf units: one string, \n separates lines as in the Japanese. Each line at most 46 bytes, total within 'max'.
  Speaker tags like （シ） become short tags: (Sylvie), (Deborah), (Jennifer), (Hatter), (Wingman).
  Keep the placeholder text ○○ wherever the Japanese has it (the game may put a name or number there).
  Keep full-width zero placeholders like ０００ or ００ exactly as they are (same characters), since the game may
  write digits into them.
  Trailing box characters such as │ or ┐ at the end of a title must be kept at the end.
- Numbers: write as ASCII digits. Years: 電脳暦ＶＣａ９年 -> "Virtual Century VCa9" first time on a page, "VCa9"
  after; ＶＣ８４年 -> "VC84".
- Keep identical Japanese lines translated identically.

## Voice
- Narration/prologue: literary, solemn, slightly ornate (it is written that way).
- MARZ command / briefings: crisp military register. 貴官 = "you" (formal address of an officer), never "Your Excellency".
- Sergeant Hatter (ハッター軍曹): loud, hot-blooded, theatrical, sprinkles English catchphrases (ガッデム -> "Goddamn!",
  ＹＥＳ！ -> "YES!", グッド・ラック -> "Good luck!", アイ・シャル・リターン -> "I shall return!"). ドンマイ -> "Don't mind!"
  in his mouth (it is his catchphrase); elsewhere "Never mind!" / "Don't worry about it!".
- Fei-Yen: cute, sing-song, ♪.
- The Rose Sisters: Sylvie Fang (シルビー・ファング, eldest, haughty commander), Deborah Bite (デボラ・バイト, brash, hot-tempered),
  Jennifer Poison (ジェニファー・ポイズン, quiet, ultra-polite, a little spacey).
- Lilin Plajiner (リリン・プラジナー): serene, gentle, formal.
- Daimon (ダイモン) / Tangram-side antagonists: cold, grandiose.
- Wingmen / allied pilots: soldiers' radio talk.

## Names and terms (use exactly)
電脳暦 Virtual Century (VC) | ＶＣａ VCa (e.g. ＶＣａ０年代 "the VCa0s")
限定戦争 the Limited War(s) | 企業国家 corporate nations | 公安 Public Security
バーチャロイド Virtuaroid | ＶＲ VR (plural VRs)
ＭＡＲＺ MARZ | ＭＡＲＺ司令部 MARZ Command | 火星 Mars | 火星戦線 the Mars Front | 赤道戦区 Equatorial Sector
ＤＮＡ DNA | ＲＮＡ RNA | ＤＮ社 DN Corporation (DN Corp.) | プラント plant (the eight great plants, 第８プラント 8th Plant)
フレッシュ・リフォー Fresh Lifo | トリストラム・リフォー Tristram Lifo | ゼロプラント Zero Plant
ハッター軍曹 Sergeant Hatter (Sgt. Hatter) | アファームド・ザ・ハッター Apharmd the Hatter
ダイモン Daimon | ダイモン・フラグメント Daimon Fragment | ダイモン・ストゥーパ Daimon Stupa
タングラム Tangram | オラトリオ・タングラム Oratorio Tangram | ゲート・オブ・タングラム Gate of Tangram
ムーンゲート Moon Gate | 月 the Moon | 木星圏 the Jupiter sphere | 地球圏 the Earth sphere
Ｖクリスタル V-Crystal | ジュピター・クリスタル Jupiter Crystal | Ｊクリスタル J-Crystal | マーズクリスタル Mars Crystal
アース・クリスタル Earth Crystal | ムーンクリスタル Moon Crystal | ワイルド・クリスタル Wild Crystal
Ｖコンバータ V-Converter | Ｖディスク V-Disc | ゲートフィールド Gate Field | オーバーロード overload
バーチャロン現象 the Virtualon Phenomenon | バーチャロン・ポジティブ Virtualon Positive | バーチャロン・フォース Virtualon Force
リバース・コンバート reverse conversion | 定位リバース・コンバート localized reverse conversion
電脳虚数空間（ＣＩＳ） Cyber Imaginary Space (CIS) | 禁制領域 the Forbidden Zone | シバルバー Xibalba
アンベル Ambel | 打撃艦隊「フォース」 the strike fleet "Force" | 白虹騎士団 the White Rainbow Knights
リリン・プラジナー Lilin Plajiner | 薔薇の三姉妹 the Rose Sisters | 毒蛇三姉妹 the Viper Sisters
アジム Ajim | ゲラン Gelan | 戦闘結晶体 combat crystal | 戦闘結晶構造体 combat crystal construct
シャドウ Shadow (汚染率 contamination rate) | ヤガランデ Yagarande | 幻獣 phantom beast | 幻獣戦機 phantom beast war machine
第七パラダイム the Seventh Paradigm | 事象崩壊要塞 the Event Collapse Fortress
Robots: テムジン Temjin | ライデン Raiden | アファームド Apharmd | ＶＯＸ VOX (ダン Dan, ジョー Joe, ジェーン Jane,
ボブ Bob, リー Lee, ルー Loo) | マイザー Myzr (マイザー・デルタ Myzr Delta) | バル・ディ・メオラ Bal Di Meora
フェイイェン / フェイ・イェン Fei-Yen | ビビッド・ハート Vivid Heart | スペシネフ Specineff | エンジェラン Angelan
ガラヤカ Galayaca | スライプナー Sleipnir | アダックス Adax | ドルカス Dorkas | ベルグドル Belgdor | タングラム Tangram
Model codes: write the Latin form, e.g. テムジン７４７Ｊ Temjin 747J, ＭＺＶ－７４７－Ｊ MZV-747-J.
Weapons: translate katakana weapon names to their English (e.g. ショルダー・ボックス・ランチャー Shoulder Box Launcher,
エジェクタブル・クロー・ランチャー Ejectable Claw Launcher). ＬＷ/ＲＷ/ＣＷ -> LW/RW/CW.
Database headings: ■　機体名称　■ "■ Unit Name ■", ■　機体解説　■ "■ Overview ■", ■　武装　■ "■ Armament ■".

## Settled after the first pass (use these; replace any variant)
Register: the original prose is unusually flowery and technical. English must keep that: ornate, dense,
elevated diction and precise technical vocabulary. Never flatten it into plain, casual English.
Business entities are "Corporation": 国際戦争公司 the International War Corporation; ＤＮ社 DN Corporation (spell out
"DN Corporation" wherever the byte limit allows; "DN Corp." only where it cannot fit); 〜社 X Corporation.
ＤＮ社最高幹部会 the DN Corporation Supreme Executive Board.
ガラヤカ Guarayakha (the game's own spelling) | 化鳥 "Monstrous Bird" | アンベル┐ Ambel┐ (keep ┐) | アンベル様 Lord Ambel
ゼロプラント / ０プラント Zero Plant | アイザーマン博士 Dr. Eisermann | ムーニー・バレー Moony Valley
火星極冠戦域 the Mars Polar Cap Theater (戦域 = theater, 戦区 = sector) | 白騎士 the White Knight | 白虹の騎士 White Rainbow Knight
白檀艦隊（ホワイト・フリート） the Sandalwood Fleet (the White Fleet) | クライグスター Kriegster | アイデルスター Idelstar
マシュー大佐 Colonel Matthew | ＯＭＧ Operation Moon Gate (OMG) | クリアリア・バイアステン Clearia Biasten
Mission screens: 成功条件 "Objective:", 制限時間 "Time Limit   :", 総敵数 "Total Enemies:".
