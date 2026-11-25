# 六线谱转写规范与 JSON 结构定义（Tab Transcription Specification）

本文档定义 `guitar-tab-to-audio.skill` 中从各类曲谱转写为 JSON 事件流（供 `scripts/render_tab_json.py` 使用）的标准规范。

---

## 1. JSON 乐谱顶层字段（完整结构）

```json
{
  "title": "曲名",
  "instrument": "steel_acoustic",
  "mic_position": "balanced",
  "bpm": 76.0,
  "time_signature": "4/4",
  "key": "C",
  "capo": 0,
  "tuning": "standard",
  "humanize": true,
  "rubato": true,
  "mode": "accompaniment",
  "flexible_measures": [],
  "tempo_map": [
    {"measure": 8, "bpm": 68.0}
  ],
  "dynamics_map": [
    {"measure": 1, "level": 0.82},
    {"measure": 5, "level": 0.90, "crescendo_to": 1.15, "end_measure": 8}
  ],
  "events": []
}
```

### 顶层核心控制字段
- `instrument`：乐器音色预设（也可由 CLI `--instrument` 覆盖）：
  - `"steel_acoustic"`（默认）：原声钢弦民谣木吉他（云杉面板 + 玫瑰木箱体 + 交感共鸣）
  - `"nylon_classical"`：古典尼龙弦吉他（雪松温暖共鸣 + 柔和泛音滚降）
  - `"electric_clean"`：电吉他清音（单线圈明亮泛音 + 长延音）
  - `"electric_overdrive"`：电吉他过载/Solo（不对称电子管软削波 + 4x12 箱体滤波 + 极长推弦延音）
- `mic_position`：麦克风与声学空间摆位（也可由 CLI `--mic` 覆盖）：`"balanced"`（默认）、`"close"`（近场干声）、`"room"`（丰富早反射与录音棚房间感）。
- `dynamics_map`：宏观段落强弱与渐强/渐弱曲线，如 `{"measure": 1, "level": 0.82}` 或 `{"measure": 5, "level": 0.88, "crescendo_to": 1.16, "end_measure": 8}`。
- `rubato`：布尔值，开启后在全曲尾小节自动施加 `+5.5%` 自然乐句收尾呼吸感。
- `time_signature`、`tuning`、`humanize`、`tempo_map`、`flexible_measures`：见严格校验、自定义调弦（`standard`/`drop_d`/`dadgad`/`eb_standard`/`open_d`/`open_g` 或 6 弦数组）与动态变速规范。

---

## 2. 演奏技法枚举（`art`，共 35 项细分技法 + `REST`）

所有未知 `art` 名称将直接触发校验报错并给出拼写建议；`notes` 数组内每个音符均支持独立指定 `art`、`velocity` 与辅助品格字段。

- **基础、断音与延音**：`"REST"`、`"PICK"`、`"STACCATO"`、`"LET_RING"` / `"TIE"`、`"CHORD"`
- **琶音与轮扫**：`"ARP_ROLL"` / `"ARP_UP"`（自动 `6→1`）、`"ARP_DOWN"`（自动 `1→6`）、`"RASGUEADO"`
- **闷音、哑音与切音**：`"PALM_MUTE"` / `"PM"`、`"DEAD_NOTE"` / `"MUTE"`、`"CHUCK_MUTE"` / `"CHUCK"` / `"CHOKE"`
- **击弦、勾弦、点弦与装饰音**：`"HAMMER"`、`"PULL"`、`"TAP"`、`"GRACE_NOTE"` / `"GRACE"`（搭配 `"grace_from"`）
- **滑音与擦弦/刮弦噪声**：`"SLIDE"`、`"SHIFT_SLIDE"`、`"SLIDE_IN"`、`"SLIDE_OUT"`、`"FRET_NOISE"`、`"PICK_SCRAPE"`
- **推弦全系列（7 项）**：`"BEND_QUARTER"`、`"BEND_HALF_UP"`、`"BEND_UP"`、`"BEND_1_5_UP"`、`"BEND_UP_REL"`、`"PREBEND_REL"`、`"BEND_HOLD_UP"`
- **揉弦、震音/轮指与颤音**：`"VIBRATO"`、`"VIB_BEND_TAIL"`、`"TREMOLO"`、`"TRILL"`（搭配 `"trill_to"`）
- **泛音全系列（4 项）**：`"HARMONIC"` / `"NH"`、`"ARTIFICIAL_HARM"` / `"AH"`、`"TAPPED_HARM"` / `"TH"`、`"SLAP_HARMONIC"` / `"SH"`
- **指弹打板与琴体鼓点（4 声部复音池）**：`"PERC_KICK"` / `"KICK"`、`"PERC_SLAP"` / `"SLAP"`、`"PERC_TAP"` / `"GOLPE"`、`"SLAP_STRUM"` / `"AM"`

---

## 3. 自动编配（`progression` & `jianpu`）与生态导出

- **C/D 类 `progression`**：12 调算法级和弦/级数解析（含离调 `2/6/4m/#4m7-5/b7/7m7-5` 与斜线低音）+ 9 种右手织体（`fingerpick_ballad`、`fingerpick_53231323`、`fingerpick_half`、`fingerpick_quarter`、`arpeggio_hold`、`arpeggio_cinematic`、`strum_folk`、`waltz_3_4`、`ballad_6_8`）。
- **E 类 `jianpu`**：内置最小左手把位移动代价函数，优先利用空弦过渡，自动抬高旋律力度（`1.12`）并压低内音伴奏（`0.72`）。
- **CLI 工具链**：
  - `--preview-tab`：打印按小节分组的 6 线 ASCII 谱与精确 `[mm:ss.s]` 时间轴。
  - `--measures START-END`：仅渲染指定小节范围用于快速局部纠错。
  - `--midi`：导出标准 SMF Type 1 `.mid`（6 弦独立通道 + 推弦/揉弦 Pitch Bend 弯音轮 + 逐音符 Velocity）。
  - `--mp3 --qa --normalize`：一键输出 320kbps MP3 并执行 `volumedetect` 响度质检与自动归一化。