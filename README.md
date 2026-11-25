# guitar-tab-to-audio.skill

将 **5 大类吉他曲谱图片（独奏/指弹六线谱、和弦图伴奏谱、纯和弦谱、12 调功能/级数谱、简谱+和弦谱）** 严格转写并合成为 **高保真立体声音频（`.wav` + `.mp3`）与标准 MIDI（`.mid`）** 的全流程 Skill 工具包。

---

## 核心能力亮点

- **视觉识谱防错与可视化核对**：支持 `--preview-tab` 输出 6 线 ASCII 谱 + `[mm:ss.s]` 时间轴、Music Sanity Linter 自动提示极端把位大跳（抓 OCR 错品），以及 `--measures START-END` 局部小节秒级重渲。
- **4 种吉他音色 + 声学空间真实感**：支持 `steel_acoustic`（钢弦民谣）、`nylon_classical`（古典尼龙）、`electric_clean`（电吉他清音）、`electric_overdrive`（电吉他电子管过载），内置琴弦交感共鸣器与 3 档麦克风摆位（`close`/`balanced`/`room`）。
- **真人乐句起伏与把位连续编配**：支持 `dynamics_map` 段落强弱/渐强渐弱曲线、`rubato` 尾句呼吸、`humanize` 律动微抖动，E 类简谱指弹编配采用最小左手把位移动代价函数。
- **35 项吉他技法 + 严格零静默回退校验**：支持逐音符独立技法与力度亮度联动、自定义调弦（Drop D / DADGAD / 降半音等）、12 调算法级和弦与级数解析。
- **打通 DAW / Guitar Pro 生态**：内置 `--midi` 零依赖导出标准 SMF Type 1 MIDI（6 弦分轨 + Pitch Bend 推弦/揉弦曲线），可直接加载 Ample Guitar / Kontakt 真实采样音源。

---

## 目录结构

```text
guitar-tab-to-audio.skill/
├── SKILL.md                                  # 五阶段防错工作流与 35 项技法规范
├── README.md                                 # 使用说明与完整目录索引
├── scripts/
│   ├── guitar_synth_engine.hpp               # C++ 多音色物理建模引擎（4 音色、交感共鸣、早反射、4 声部复音打板池）
│   └── render_tab_json.py                    # 严格校验、ASCII 预览、标准 MIDI 导出、局部渲染与一键 MP3/QA 脚本
├── references/
│   └── tab-transcription-spec.md             # JSON 字段、35 项技法、音色/麦位/强弱/MIDI 规范
└── examples/
    ├── all_techniques_showcase.json          # 全 35 项吉他技法 + 混合技法 + 力度 + 变速综合演示
    ├── expressive_solo_overdrive.json        # 电吉他过载 Solo + 段落强弱 + Rubato 乐句呼吸示例
    ├── functional_chord_example.json         # D 类功能谱/级数谱自动编配示例（1=C 卡农进行）
    ├── jianpu_fingerstyle_example.json       # E 类简谱旋律 + 级数自动把位连续指弹编配示例
    ├── drop_d_example.json                   # Drop D 特殊调弦 + 电影感琶音 + 离调级数示例
    ├── red_high_heels_score.json             # 完整指弹编配实例：《红色高跟鞋》（C 调夹 2 品）
    ├── sample_test.json                      # 最小冒烟测试乐谱
    ├── grey_track_solo.cpp                   # A 类谱独立 C++ 实例：Beyond《灰色轨迹》尾奏 Solo
    └── piaoliuping_accompaniment.cpp         # B 类谱独立 C++ 实例：《漂流瓶》完整弹唱伴奏
```

---

## 快速使用方式

```bash
# 全功能一键渲染（ASCII 时间轴核对 + 标准 MIDI + 320kbps MP3 + 响度质检与自动归一化）
python3 scripts/render_tab_json.py examples/all_techniques_showcase.json ./output/showcase.wav \
  --preview-tab --midi --mp3 --qa --normalize

# 切换古典尼龙吉他 + 房间麦氛围
python3 scripts/render_tab_json.py examples/jianpu_fingerstyle_example.json ./output/classical.wav \
  --instrument nylon_classical --mic room --mp3 --qa

# 仅快速重渲染第 2~3 小节用于局部核对
python3 scripts/render_tab_json.py examples/red_high_heels_score.json ./output/m2_3.wav \
  --measures 2-3 --preview-tab
```