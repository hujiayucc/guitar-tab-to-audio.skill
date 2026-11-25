---
name: guitar-tab-to-audio
description: 吉他曲谱图片转高保真吉他音频（WAV/MP3）与标准 MIDI（.mid）全流程工作流。覆盖5大类曲谱（单音独奏/指弹六线谱、和弦图分解扫弦谱、纯和弦谱、12调功能谱/级数谱、简谱旋律+和弦自动指弹编配），内置视觉识谱防错与反复结构核对协议、ASCII六线谱时间轴预览、局部小节快速重渲、35项吉他演奏技法、4种吉他音色（钢弦木吉他/尼龙古典/电吉他清音/电吉他过载）、琴弦交感共鸣、宏观段落强弱起伏与标准MIDI导出。
---

# guitar-tab-to-audio.skill

本 Skill 定义了从 **各类吉他曲谱图片** 到 **高保真吉他音频（`.wav` + `.mp3`）与 DAW/Guitar Pro 通用标准 MIDI（`.mid`）** 的全流程防错制作规范。

---

## 全流程五阶段工作流（Workflow）

### 阶段 1：视觉识谱防错、反复结构展开与五类谱型分类

收到曲谱图片后，**禁止直接盲目写代码渲染**，必须先完成三项防错核对：

1. **图像质量与疑似模糊位标注**：
   - 先确认图片清晰度、是否存在多页截图拼接或反光遮挡；对高风险易混淆符号（`0/6/8`、`1/7`、两位数品格 `12` 被拆成 `1 2`、附点与小节线、弦号偏行）进行逐行核对，并用当前小节的和弦音/调内音交叉验证每个疑似品格的乐理合理性。
2. **反复与曲式结构显式展开表**：
   - 遇到 `||: :||`、`1.`/`2.` 跳房子、`D.S. al Coda`、`Fine` 或跨页衔接时，先列出完整的线性小节演奏顺序（例如 `Intro(1-4) -> A(5-12) -> B(13-20) -> A2(5-11, 21)`），确保零漏段、零多遍。
3. **全局元数据与五类谱型判定**：
   - 提取调号 `1 = Key`、变调夹 `Capo`、特殊调弦 `tuning`、拍号 `time_signature`、速度 `bpm` 与 `tempo_map`、段落强弱 `dynamics_map`、适配乐器 `instrument`（钢弦木吉他 `steel_acoustic` / 古典尼龙 `nylon_classical` / 电吉他清音 `electric_clean` / 电吉他过载 `electric_overdrive`）。
   - 判定谱型：**A 类**（品格数字独奏/指弹六线谱）、**B 类**（和弦图+`×`/琶音伴奏六线谱）、**C 类**（纯和弦/歌词和弦谱）、**D 类**（12 调功能/级数谱）、**E 类**（简谱旋律+和弦/级数谱，支持内置 `"jianpu"` 自动把位连续指弹编配）。

---

### 阶段 2：结构化转写、乐句表现力编配与严格校验

1. **严格校验（零静默回退）**：
   - `scripts/render_tab_json.py` 强制逐小节核对拍数是否匹配 `time_signature`（弱起或尾奏自由延长需用 `flexible_measures`、`allow_pickup: true` 或事件级 `"fermata": true` 显式声明）。
   - 任何非法 `art`、`pattern`、`degree`/`chord` 或越界弦号/品格直接报错并给拼写建议，同时运行 **Music Sanity Linter** 自动提示相邻音符跨度 > 8 品的疑似 OCR 错品。
2. **宏观音乐性与呼吸感（告别机械感）**：
   - 使用 `dynamics_map`（如主歌 `0.82` -> 副歌 `1.14` 或 `crescendo_to` 渐强曲线）、`humanize: true`（自然强弱拍律动重音 + ±3.5ms 微抖动）和 `rubato: true`（乐句尾小节自然微慢），让音频具备真人演奏的起伏与呼吸。
3. **12 调和弦/级数引擎与 E 类把位连续简谱编配**：
   - C/D 类支持全部 12 调、离调级数（`2/6/4m/#4m7-5/b7/7m7-5`）、斜线低音（`1/3, 5/7, 4/5, 6m/5`）与 9 种右手织体（含 `arpeggio_cinematic`）。
   - E 类 `"jianpu"` 模式基于最小左手把位移动代价函数自动选弦选品，优先利用空弦过渡，自动抬高主旋律力度（`1.12`）、压低内音伴奏（`0.72`）。

---

### 阶段 3：35 项吉他技法物理建模、多音色与声学空间

通过 `scripts/guitar_synth_engine.hpp` 的 6 弦独立物理振子、4 声部复音打板池、琴弦交感共鸣器与早反射空间网络实现：

- **10 大体系、35 项技法**（支持单事件内逐音符独立 `art` 与 `velocity` 力度亮度联动）：
  - 基础/断音/延音：`PICK`、`STACCATO`、`LET_RING`/`TIE`、`CHORD`
  - 琶音/轮扫：`ARP_ROLL`/`ARP_UP`（自动 `6→1`）、`ARP_DOWN`（自动 `1→6`）、`RASGUEADO`
  - 闷音/哑音/切音：`PALM_MUTE`、`DEAD_NOTE`、`CHUCK_MUTE`
  - 击勾点弦与装饰音：`HAMMER`、`PULL`、`TAP`、`GRACE_NOTE`
  - 滑音与擦弦/刮弦：`SLIDE`、`SHIFT_SLIDE`、`SLIDE_IN`、`SLIDE_OUT`、`FRET_NOISE`、`PICK_SCRAPE`
  - 推弦全系列（7 项）：`BEND_QUARTER`、`BEND_HALF_UP`、`BEND_UP`、`BEND_1_5_UP`、`BEND_UP_REL`、`PREBEND_REL`、`BEND_HOLD_UP`
  - 揉弦/震音/颤音：`VIBRATO`、`VIB_BEND_TAIL`、`TREMOLO`、`TRILL`
  - 泛音全系列（4 项）：`HARMONIC`、`ARTIFICIAL_HARM`、`TAPPED_HARM`、`SLAP_HARMONIC`
  - 指弹打板鼓点（4 声部复音）：`PERC_KICK`、`PERC_SLAP`、`PERC_TAP`、`SLAP_STRUM`
- **4 种吉他音色模型（`instrument`）**：`steel_acoustic`（钢弦民谣）、`nylon_classical`（古典尼龙）、`electric_clean`（电吉他清音）、`electric_overdrive`（电吉他电子管过载 + 4x12 箱体）。
- **声学真实度增强**：内置 **琴弦交感共鸣（Sympathetic Resonance）** 与 **6 抽头早反射 + 3 档麦克风摆位（`mic_position`: `close` / `balanced` / `room`）**。

---

### 阶段 4：可视化核对、局部快速重渲与标准 MIDI 生态联动

1. **先看 ASCII 六线谱时间轴再交付**：
   - 使用 `--preview-tab` 输出按小节排列的 6 线 ASCII 谱与精确 `[mm:ss.s]` 时间轴，便于快速定位任意小节的弦号、品格和时间戳。
2. **局部小节 1 秒重渲**：
   - 若用户指出“第 12 小节有个音不对”，修改 JSON 后可直接用 `--measures 11-13` 秒级试听局部片段，确认无误再渲染全曲。
3. **导出标准 MIDI（`--midi`）**：
   - 同步输出标准 SMF Type 1 `.mid` 文件（6 弦独立通道 + 推弦/揉弦 Pitch Bend 曲线 + 精确 Velocity），可直接拖入 Guitar Pro、Logic Pro、Cubase、FL Studio 或 Ample Guitar / Kontakt 加载录音室采样音源。

---

### 阶段 5：一键全流程渲染、转码与响度质检（QA）

```bash
python3 scripts/render_tab_json.py input_score.json ./output/song.wav \
  --preview-tab --midi --mp3 --qa --normalize
```
- 脚本自动处理 `/tmp` 挂载 `noexec` 的环境回退，执行后清理临时编译产物。
- `--qa` 自动校验 `max_volume ∈ [-6.0, -1.5] dB` 与 `mean_volume ∈ [-20.0, -12.0] dB`，`--normalize` 在超标时自动修正增益。
- 交付时向用户返回 `.mp3`、`.wav`、`.mid` 路径、`[QA PASS]` 响度报告及段落时间轴说明。