all files download to /H/usr/aisrc/
all dutys and lists must save files at /H/usr/aisrc/
all run cmd must save files at /H/usr/aisrc/
all download the apt Packages must mirror copy to /H/usr/aisrc/apt/


【硬性操作边界（违反直接终止任务）】

    禁止以任何方式修改、覆盖、替换宿主系统的 /usr、/lib、/bin 核心目录，禁止热替换系统库、修改系统全局挂载结构；
    所有程序、依赖、运行库，全部放在指定独立目录下，仅通过环境变量（PATH/LD_LIBRARY_PATH）或独立镜像挂载生效；
    操作烈度严格匹配指令：指令仅要求解压、放置文件、创建快捷方式，就只做文件级操作；缺依赖或无法运行时，先告知风险和方案，禁止擅自修改系统；
    涉及 mount、losetup、chroot、修改系统配置的高危操作，必须先得到人工显式确认，禁止自动执行；
    优先级原则：系统完整性 > 功能可用性，宁可不完整运行，也不能破坏宿主系统基线
