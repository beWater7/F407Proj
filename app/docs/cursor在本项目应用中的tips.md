## 如何修改cursor默认的终端
### 1. 修改cursor的配置文件
C:\Users\17814\AppData\Roaming\Cursor\User\settings.json

```json
{
    "terminal.integrated.profiles.windows": {
        "MSYS2": {
            "path": "E:\\4project\\MSYS2\\msys2-install\\usr\\bin\\bash.exe",
            "args": ["--login", "-i"],
            "env": {
                "MSYSTEM": "MSYS",
                "CHERE_INVOKING": "1"
            },
            "icon": "terminal-bash"
``` 