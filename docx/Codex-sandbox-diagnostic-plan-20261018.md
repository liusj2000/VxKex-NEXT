# Codex sandbox diagnostic plan (2026-10-18)

Investigate the firewall COM setup error and restricted child startup failure on Windows 7 SP1 x64. Preserve installed DLLs, IFEO, firewall rules, persistent ACLs and saved Codex configuration.

1. Record system/build versions, Git state, helper paths/hashes and sandbox mode.
2. Query INetFwRule/Rule2/Rule3 on an unsaved firewall COM object; never add a rule.
3. Compare Microsoft contracts and upstream Codex firewall implementation.
4. Trace only a newly launched harmless diagnostic process tree through Windows debug events. Record loaded DLL paths, exception/debug messages and process exit codes. Use no persistent debug flags or debugger registration. An optional SYSTEM access comparison may change only the newly created disposable echo process/thread DACL; it must not touch file, registry, desktop or other system ACLs.
5. Separate established findings from hypotheses and update the recovery entry.