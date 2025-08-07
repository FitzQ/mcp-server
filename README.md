
# Switch MCP-Server

本项目是一个用于 Nintendo Switch 的 MCP Server，提供如下功能：

- tools
  - 手柄按键控制注入（支持按键、摇杆、六轴等）
  - 获取当前ns画面
- resources
- prompts

## 编译方法

1. 安装 [devkitPro](https://devkitpro.org/) 和 libnx。
2. 配置 `DEVKITPRO` 环境变量。
3. 在项目根目录下运行：
   ```
   make
   ```
4. 编译产物会自动打包到 `out/` 目录。

## 使用说明

1. 将 `out/atmosphere/contents/010000000000B1C0` 目录下的内容复制到你的 Switch SD 卡`/atmosphere/contents`目录下。
2. 开关位置：Hekate Toolbox -> 后台服务 -> mcp-server
3. 服务配置
    ```json
    {
      "servers": {
        "switch-mcp-server": {
          "type": "streamableHttp",
          "url": "http://{你的switch ip}:12345/mcp"
        }
      }
    }
    ```

## 当前已知问题

1. 有时候服务打不开，可以重启下switch就可以了。原因和这个程序启动时申请2MB堆内存有关。才2MB。。。取小了的话那个获取ns画面的功能就不能正常运行。
2. switch息屏后会停止程序，重新开屏后服务没法自动恢复，需要再到Hekate Toolbox下关闭&打开mcp-server。

## 主要目录结构

- `source/`         主体源码
- `source/tools/`   MCP Server tools
- `source/util/`    日志等通用工具
- `source/third_party/`  第三方库
- `out/`            编译输出

## 致谢

- [devkitPro](https://devkitpro.org/)
- [libnx](https://github.com/switchbrew/libnx)
- [stb_base64](https://github.com/nothings/stb)

---
如有问题或建议，欢迎 issue 反馈。
