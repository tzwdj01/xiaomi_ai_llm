# S12 / UBIFS 适配：一次设备记录

此目录记录一台自报 **S12、system1 ROM 1.74.18** 的音箱如何运行首轮 DeepSeek 问答。它是对上游项目的实验性适配，不是通用安装包；主项目的 S12A 实机结论不自动适用于 S12。不要直接执行这里的启动脚本或套用 S12A 刷机命令。

## 设备差异

- 内核为 3.14.29、用户态 ELF 为 64 位 AArch64；无线芯片为 BCM4345，存储显示为 256 MiB NAND。
- 当前使用 boot1/system1。设备的 `/proc/mtd` 将 `boot0`、`boot1`、`system0`、`system1`、`data` 分别标为 `mtd1`、`mtd2`、`mtd3`、`mtd4`、`mtd5`；设备和固件不同，编号不能外推。
- 根目录是 UBIFS。客户端依据实际根挂载、UBI 的 `mtd_num` 和 `/proc/mtd` 的 `system1` 名称识别槽位，同时保留原项目的 `/dev/mtdblock5` 路径判断。唤醒 hook 使用相同逻辑。
- 此分支移除了客户端启动时对 `kws`、`tail`、`curl`、`aplay` 和 `arecord` 的全局 `killall`，避免无差别停止其他任务；目标服务的多实例及回退行为仍需在其他设备单独验证。

## 已验证的范围

仅在一台设备上验证：串口 failsafe、临时和持久公钥 SSH、正常系统冷启动、DeepSeek 直连、AArch64 静态 EdgeTTS 握手和实际播报、原生唤醒、首轮模型回答、客户端自启动。联网、播放、语音识别和 API 费用取决于对应云服务。

`native_first.s12.env.example` 记录这次的首轮配置。实际 API 密钥只能保存在设备私有配置中。音箱端 `ettsc` 是使用项目源码构建的 **aarch64-unknown-linux-musl** 静态产物；上游 `build.sh` 的默认目标是 32 位 ARM，构建目标必须按实际设备确定。构建过程不分发固件或设备二进制。

## 2026-11-08 联网搜索、播放和灯效适配

- 上游联网模块使用裸 `@` 检查 JSON 根类型，此设备的旧 `jsonfilter` 不接受该表达式。这里先把响应包装成对象字段，再检验字段类型；未降低响应长度、NUL、HTTP 错误和类型检查。用音箱上的真实解析器和账号做过隔离测试：不联网回答、联网搜索、指定网页读取与完整回答流通过；实际语音搜索也已在日志中看到来源与回答。搜索/网页读取以及模型判断可能收费。
- `LLM_WEB_FALLBACK=fail` 在判断或检索失败时提示失败，不把未核实的答案当作联网结果。`LLM_WEB_SPECULATIVE=0` 选择串行请求，减少额外候选请求；可能增加首声等待。
- 旧 BusyBox 的 `sleep` 不支持小数，`S12_FRACTIONAL_SLEEP=1` 时将客户端和唤醒 hook 的小数秒等待交给系统 `usleep`，防止流式等待循环空转。安装前应确认固件确实提供 `usleep`。
- `/sys/devices/i2c-3/3-003c/led_rgb` 是本机的 IS31FL3236A 灯控路径；已验证 sysfs 单灯写入，主脚本和 hook 均从 `LED_RGB` 配置获取路径。真实交互中的灯光效果仍需人工确认。
- 普通 `aplay` 虽返回成功，但因 Dirac 未初始化报错。`s12_dirac_init.c` 是只在播放进程中载入的 64 位初始化器；`s12_dirac_aplay.sh` 限定本次 system1、播放器及配置指纹。开启提示音预热的真实搜索请求中用户反馈**没有声音**，因而先恢复了整段 MP3。随后用户确认独立 PCM、暂停原生播放器后的 PCM、FIFO PCM、一次不带接管提示音的短问答，以及实际联网搜索的“搜索中，请稍等”和后续回答均能听到：目前逐句播放已重新启用，`LLM_ROUTE_BEEP=0` 和 `LLM_END_BEEP=0`。这些结果不证明提示音预热无声的根因、长回答或冷启动后稳定；不要自行打开提示音冒充已验证。播放程序退出成功和 Dirac 初始化成功均不能代替声学确认。若静态基线变化，客户端启动时退回整段 MP3。

构建本次实验用初始化器的示例（Zig 0.14.1，产物必须在设备上独立验收）：

```sh
zig cc -target aarch64-linux-gnu.2.19 -shared -fPIC -Os -Wall -Wextra -Werror \
  device/s12_ubifs/s12_dirac_init.c -ldl -o s12_dirac_init.so
```

此 wrapper 中的 MD5 仅用于拒绝与本次测试不同的文件，并非可信来源证明；不同构建的共享库不能仅靠更改指纹放行。完整部署需要备份客户端与私有配置、确认空闲、验证 ALSA 与语音服务，不能只复制示例配置。回退时使用本机本次操作生成的备份，停止助手并恢复该备份中的客户端与配置，再重新启动；备份包含密钥，不应公开。

## SSH 入口与恢复边界

`sshen_s12.sh` 是本次 system1 上使用的启动 hook 的脱敏版本，仅供审阅。它依赖事先准备的本机公钥、主机密钥和相应 Dropbear 配置；不会独立完成安全安装，也没有经过其他设备测试。设备使用端口 22 和公钥认证，禁用了密码认证，保留原授权文件并在启动时通过 bind mount 临时替换。

在这台设备上，原厂启动脚本 `/data` 挂载失败分支包含擦除和重建。该设备的 system1 已做文件级修改，去除该分支并保留原文件备份。**挂载失败后的完整启动和服务行为没有故障注入验证**；不要把这种修改视作已验证的通用恢复方案。原厂 OTA 和自动槽位切换仍可能改变当前系统。

设备上可读取的五个分区已备份并作第二次读取校验；`nandboot` 的一次读取发生 ECC 失败，诊断数据不完整。备份不包含完整 OOB、坏块处理和经过验证的恢复流程。**本仓库不包含或分发这些备份、原厂固件、启动环境、设备密钥和个人日志。**

## 未完成与不兼容项

- system0 的持久 SSH、助手自启动、正常启动与回滚尚未实机验证；仅配置了 system1。不要直接修改活动根分区或覆盖内核来复制本次操作。
- 原生连续追问和高级判停尚未适配。项目现有的 native ASR 组件面向已验证 S12A 固件与 ARM32 原生进程；这台设备的原生进程是 AArch64，固件 ABI 与组件基线不匹配。不要绕过安装器校验或把 ARM32 SO 直接注入 64 位进程。
- ABI 研究：本机 `libaivs-message-util.so` 的顶层、upward 和 prepare 消息描述符分别报告结构体大小 48、72 和 40 字节，相关字段偏移分别为 32、40 和 24。`abi_probe.c` 和 `test_s12_prepare.c` 在独立进程中使用设备描述符验证 `activate_mode=1` 的副本序列化；后一测试还验证原对象不变、非目标消息透传、序列化变长时拒绝写入。`test_s12_json.c` 在独立进程使用设备 JsonCpp 验证 ASR-only 控制项、指令隔离和旧结果隔离。测试没有连接原生服务或发送语音。这些结果**不能证明可以安全唤醒、连续追问或接管判停**。
- `native_asr_s12_experiment.c` 是仅供独立测试的 64 位适配草稿，编译被限制在单元测试中。`test_s12_followup_safety.c` 使用设备的消息描述符和模拟回调验证了消息副本、普通消息透传、唤醒交接、追问标记保护和取消后丢弃旧回调；这些交接是模拟测试，没有在正在运行的音箱对话中验证。唤醒词缓冲完成通知、采集链路和完整隔离尚未验证，不能将此草稿加载进音箱服务。
- 本机 VPM 的 ASR 回调使用不同于 S12A 的数据结构。限定时间的单回调观察在用户一次正常唤醒问答中计到 108 个 1920 字节音频块、另有三个其他包，长度检查没有发现异常；实验只记录类型与长度，没有保存语音。说话期间音箱能够正常应答，但此后立即发生了下述内核崩溃。探针当时处于加载状态，因此不能排除其影响；之前也曾在没有探针时发生自行重启。此结果确认回调在本次问答中被调用，并不证明音频格式、长期稳定性或完整判停链路。`vpm_pcm_probe_experiment.c` 仍仅允许模拟测试编译。
- 本机运行中的原厂进程的回调槽位与静态分析结果一致；固件另有唤醒词数据处理回调和“唤醒前 ASR 缓存”路径。`test_s12_ivw_emulator.py` 在本地模拟器中执行固件片段并替换外部依赖，发现指定空长度输入会打开缓冲门控。实验只适用于对应固件，无法证明实际调用时机、线程安全或对真实识别的影响；不能凭固定地址调用该回调或据此开启连续追问。
- 追问客户端在请求识别前会主动设置忙碌标记，原厂追问工作线程要求该标记存在。此前研究时曾误认为这个判断写反，并短暂在研究分支中改错；该错误没有安装到音箱。当前已恢复原有判断语义、将路径统一使用 `NATIVE_BUSY_FILE`，并用 `test_native_asr_worker.c` 在独立进程验证缺少标记时拒绝、有标记时允许模拟唤醒，以及取消后丢弃旧完成回调。研究代码尚未作为追问服务安装到本机。
- 原生唤醒透传探针捕获了 `libmdspeech.so` 的一次唤醒回调，原生应答正常；另在 `libvpm.so` 找到了本机的 `xaudio_register_callback` 和 `register_vpm_data_upload_callback`，它们与 S12A 的函数签名不同。临时 VPM 透传探针确认了两项注册，但测试期间设备意外重启，随后启动参数标记为 `kernel_panic`；临时日志已随 `/tmp` 清空，无法确定是探针、服务重启还是其他原因导致。重启后原厂音频进程、助手进程和 SSH 已恢复，用户确认小爱可以应答。之后启用串口日志，再用只观察注册、不替换任何回调的临时探针运行三分钟并自动恢复，期间没有重启；本次没有收到语音测试确认，不能据此断定崩溃根因或证明完整语音链路。**不要在其他设备运行这些探针或把它们当成安装器；应先确定崩溃根因。**
- 后来设备在探针已卸载数小时后再次自行重启，启动参数仍标记 `kernel_panic`。本机当时将内核串口日志级别设为 0，串口日志只保存了复位和启动信息，没有崩溃堆栈；两次重启的根因仍未知。诊断时暂时把控制台级别调到 4 并验证标记能进入本地串口日志，设置有 30 分钟自动恢复保护，不写入固件；这是一次临时诊断，不表示修复了重启问题。
- 随后一次真实唤醒后又发生内核崩溃；本地串口栈显示 `wakeup.sh` 写入 sysfs，经过 `is31fl3236a` 的 `led_control`、`AddItem`、`FreeNode`，在 `remove_vm_area` 处发生空指针异常。原厂唤醒脚本与新增灯效都可能涉及灯光驱动，栈本身不能单独证明写入来源，也不能证明其他重启同因。当时单回调探针已加载。为降低风险，本机已暂时设置 `LED_FEEDBACK_ENABLED=0`，并为客户端的直接 LED 写入和关闭灯效调用补充该开关保护；配置和客户端已私有备份。`test_led_guard.sh` 使用普通临时文件与替身命令检查开关，没有访问真实 LED。用户随后各验证了一次原生回答和 DeepSeek 首轮回答，均正常且当时没有重启。当前不能宣称重启问题已修复或建议重新开启灯效，仍需长期稳定性验证。
- 模型独立验证：`test_s12_ort.c` 在独立进程载入 Silero v5（SHA256 `6b99cbfd39246b6706f98ec13c7c50c6b299181f2474fa05cbc8046acc274396`）及 sherpa-onnx 1.10.36 官方 AArch64 JNI 包内的 ONNX Runtime 1.17.1（SHA256 `89abc6da8d7ac84e5270349fe8a44ee7469d9a958797eed004f1a3f5c7df21d4`），用 16 个合成静音窗口完成推理，耗时 57 ms；没有读取麦克风。实验 `s12_silero.c` 使用同一个模型和项目所选的阈值，在由设备自带提示音与静音拼接的 12.85 秒测试信号上，产生了与官方 Windows x64 sherpa-onnx 1.10.36 相同的 6 个逐帧事件；重置后的重放一致，非法参数被拒绝。它只输出检测状态，不保存音频片段；20 秒上限及错误状态均要求调用方放弃本轮。该实现参考 sherpa-onnx 的 Apache-2.0 代码，其许可随本目录提供。这个单一测试信号不构成真实语音、噪声、性能或长期稳定性验收。旧内核的 `cpuinfo` 打印了 `kernel_max=-1` 解析错误。模型与运行库合计的试验 gzip 包约 6.55 MiB，本机 `/data` 剩余约 10.6 MiB；正式安装仍须核算程序、备份和安全余量。此试验没有安装高级判停，也没有连接实时 PCM。模型来自 [Silero v5.0](https://github.com/snakers4/silero-vad/tree/v5.0)，运行库来自 [sherpa-onnx v1.10.36 发布页](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.10.36)；仓库不包含这些第三方二进制。
- `test_s12_decision.c` 在独立进程将上述模型结果送入上游的帧与判停决策结构，验证了测试信号的句末、600 个 10 ms 静音帧产生的无声退出条件，以及过期提议、未消费音频和取消状态被拒绝。`test_s12_decision_ipc.c` 使用两个独立进程和共享内存按 10 ms 间隔重放该信号，也在第 765 帧获得句末提议，并验证取消后不接受结果。两项测试没有连接原生服务或发送停止指令；跨进程异常退出、设备真实音频和端到端控制尚未验证。
- `test_s12_endpoint_guards.c` 仅选择上游不依赖 ARM32 消息布局的保护测试，在 S12 独立进程中通过了无声退出、模拟模型进程死亡后的拒绝、再次唤醒交接，以及四轮和十二轮池切换；停止和音频回调均为测试替身。这不等于完整的 S12 判停安装或真实设备采集验收。
- 本机尝试一次合成音频的 `mibrain ai_service` 文件式 ASR，返回 `code=-1`；这只说明该次测试未成功，不能据此断言所有识别接口不可用。
- 连续追问、首轮本地判停、OTA 拦截、长期稳定性以及其他硬件固件均未验收。具体适配应先完成 ABI 研究、独立测试、空间评估和回滚演练。

本次参考上游 [`open-speaker-llm/xiaomi_ai_llm`](https://github.com/open-speaker-llm/xiaomi_ai_llm)，保留原项目许可及第三方组件许可证。此记录不含账号、网络地址或认证材料。

## 独立实验构建

下列程序只在独立进程中验证设备库和模型；它们不是安装器，不加载进原生服务，也不会启用追问或判停。先自行获取并校验 ONNX Runtime 1.17.1 的 `onnxruntime_c_api.h`（SHA256 `7e47eb78563da119f740dc0ddfda96800e779e0bcbd169a9a49e9c10746c4cd5`），令 `ORT_INCLUDE` 指向头文件目录：

```sh
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror \
  device/s12_ubifs/test_s12_prepare.c -ldl -o test_s12_prepare
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -Wl,--export-dynamic \
  device/s12_ubifs/test_s12_json.c -ldl -lpthread -o test_s12_json
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -Wl,--export-dynamic \
  device/s12_ubifs/test_s12_followup_safety.c -ldl -lpthread -o test_s12_followup_safety
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -Wl,--export-dynamic \
  device/native_asr/test_native_asr_worker.c -ldl -lpthread -o test_native_asr_worker
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -I"$ORT_INCLUDE" \
  device/s12_ubifs/test_s12_ort.c -ldl -lrt -o test_s12_ort
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -I"$ORT_INCLUDE" \
  device/s12_ubifs/test_s12_silero.c device/s12_ubifs/s12_silero.c -ldl -o test_s12_silero
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -I"$ORT_INCLUDE" \
  device/s12_ubifs/test_s12_decision.c device/s12_ubifs/s12_silero.c -ldl -o test_s12_decision
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -I"$ORT_INCLUDE" \
  device/s12_ubifs/test_s12_decision_ipc.c device/s12_ubifs/s12_silero.c -ldl -lrt -o test_s12_decision_ipc
zig cc -target aarch64-linux-gnu.2.19 -Os -Wall -Wextra -Werror -Wl,--export-dynamic \
  device/s12_ubifs/test_s12_endpoint_guards.c -ldl -lpthread -o test_s12_endpoint_guards
```

将编译产物、核验后的模型和运行库临时传至设备后，`test_s12_prepare`、`test_s12_json`、`test_s12_followup_safety` 与 `test_s12_endpoint_guards` 不带参数运行；`test_s12_ort` 接收运行库和模型路径。`test_s12_silero` 和两项 `test_s12_decision` 额外接收单声道 16 kHz、S16LE 的测试信号文件路径。测试应在独立临时目录进行，不覆盖 `/data`，并自行处理第三方组件的许可。模型代码的 20 秒约束与错误处理仍需在完整控制链路中验证。

`test_native_asr_worker` 也不需要参数；`sh device/s12_ubifs/test_led_guard.sh /path/to/native_first_client.sh` 只在临时文件上测试灯效开关。离线的 `python device/s12_ubifs/test_s12_ivw_emulator.py /path/to/mipns-xiaomi` 需要自行安装 `unicorn==2.1.4` 和 `pyelftools`、自备对应设备固件；MD5 只是拒绝其他构建的兼容检查，并非供应链认证。模拟器替换了外部函数，不能作为真机安全验收。
