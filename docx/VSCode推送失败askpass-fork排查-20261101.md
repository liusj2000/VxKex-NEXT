# VS Code 推送失败：askpass fork 错误（2026-11-01）

## 本轮核查

- 用户截图是 git push origin main:main，在认证提示阶段报 askpass.sh: fork: Resource temporarily unavailable，随后 unable to read askpass response、/dev/tty 不存在、could not read Username for https://github.com。
- 实际日志：D:\VSCode-win32-x64-1.107.1\data\user-data\logs\20261101T161016\window1\exthost\vscode.git\Git.log；17:41:00 与 17:49:06 两次同样失败，分别耗时 521427ms、467012ms。
- askpass.sh 需要通过 mktemp、VS Code Electron/Node、cat、rm 等子进程返回凭据；fork 失败使认证链中断。日志没有显示 GitHub 拒绝仓库写权限或分支冲突。
- HEAD=f4a9438，main 相对本地 origin/main 缓存领先 3 个提交，开始检查时工作区干净；本轮没有 fetch，未核实实时远端引用。用户提交仍保留在本地。
- Git 为 2.46.2.windows.1；本轮独立 bash --noprofile --norc -c /usr/bin/true 返回 0，所以未持续复现 fork 失败。
- 采样物理可用内存 415984 KiB（约 406 MiB），总物理内存 4193848 KiB；可用虚拟内存 1537812 KiB。资源压力只是线索，未证明内存不足、VxKex 注入或其他单一根因。
- git config 当前没有 credential.helper 配置。已安装 git-credential-manager.exe --version 返回 -2146232576（0x80131700），未成功启动；没有将其启用为助手，也没有运行登录或读取凭据。它不是截图中已证实的直接失败点。

## 结论与下一步

直接失败点为本地 VS Code askpass 的子进程创建，导致 Git 无法取得用户名；“No such file or directory”在这里不代表仓库目录丢失。

先保存工作，关闭多余终端/应用并重启 VS Code 后重试一次同步。若继续出现 fork 错误，应复现 VS Code 环境中的 shell 子进程创建并检查相关加载日志，再决定修复认证桥或可用凭据助手；不能直接认定令牌失效或修改系统缓解策略。

本轮仅诊断与文档更新，未推送、改远端、重置提交、写凭据、调整 Git 认证配置或重启用户窗口。
