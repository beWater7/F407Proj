  const sidebar = document.getElementById('sidebar');
  const menuBtn = document.getElementById('menuBtn');
  const contentWrapper = document.getElementById('contentWrapper');

  menuBtn.addEventListener('click', () => {
    sidebar.classList.toggle('-translate-x-56');
    contentWrapper.classList.toggle('ml-48'); // 展开时左移一点
  });

  function showPage(id) {
    document.querySelectorAll('.page').forEach(p => p.classList.add('hidden'));
    document.getElementById(id).classList.remove('hidden');
  }
  // 注册插件
  // 注册插件和元素
  Chart.register(
    ChartStreaming,
    ChartDataLabels,
    Chart.LineElement,
    Chart.PointElement,
    Chart.LinearScale,
    Chart.TimeScale,
    Chart.Title,
    Chart.Tooltip,
    Chart.Legend
  );
  // 用数组缓存最近24小时数据
  const cpuHistory = [];
  const timeLabels = [];

  // 更新间隔（毫秒），比如 1分钟=60000ms, 30秒=30000ms
  let updateIntervalMs = 60000; // 默认每分钟更新一次
  let currentCPUValue = 0;
  function formatUptime(uptime) {
    const timeParts = uptime.split(':');  // 拆分时间字符串
    const hours = parseInt(timeParts[0], 10);
    const minutes = parseInt(timeParts[1], 10);
    const seconds = parseInt(timeParts[2], 10);
    return hours * 3600 + minutes * 60 + seconds;  // 转换为秒
  }
  // 更新系统信息
	function updateSystem() {
	  fetch('/protocol/system/info')
		.then(r => r.json())
    .then(d => {
      // 确保 d.data 存在，并且包含必要的字段
      if (!d.data || !d.data.uptime || !d.data.weather || !d.data.temperature || !d.data.systime) {
        throw new Error("数据缺失");
      }

      // 更新系统运行时间
      document.getElementById('upTime').textContent = '运行时间: ' + d.data.uptime;

      // 更新天气和温度
      const weatherEl = document.getElementById('weather');
      const tempEl = document.getElementById('temperature');
      if (weatherEl && tempEl) {
        const weather = d.data.weather;
        const temperature = d.data.temperature;

        const weatherEmojiMap = {
          '晴': '☀️',
          '雨': '🌧️',
          '雪': '❄️',
          '多云': '☁️',
          '雷阵雨': '⛈️'
        };

        let emoji = '☁️'; // 默认值
        for (const key in weatherEmojiMap) {
          if (weather.includes(key)) {
            emoji = weatherEmojiMap[key];
            break;  // 找到后跳出循环
          }
        }

        weatherEl.textContent = `${emoji} ${weather}`;
        tempEl.textContent = `${temperature}°C`;
      }

      // 更新系统时间
      document.getElementById('systemTime').textContent = '系统时间: ' + d.data.systime;

      // 更新 CPU/MEM 图表
      if (cpuMemChart) {
        cpuMemChart.data.datasets[0].data = [d.data.cpu_usage, d.data.mem_usage];
        cpuMemChart.update();
      }

      // 更新实时滚动图的 CPU 值
      currentCPUValue = d.data.cpu_usage;

    })
    .catch(err => {
      // 处理错误信息
      console.error('获取系统数据错误:', err);
      // 错误提示
      document.getElementById('upTime').textContent = '运行时间: ...';
      document.getElementById('weather').textContent = `☀️ 晴`;
      document.getElementById('temperature').textContent = `  °C`;
      document.getElementById('systemTime').textContent = '系统时间: ...';
    });
	}

	// 定时更新
	setInterval(updateSystem, 5000);
	updateSystem();


    // 文件上传
    document.getElementById('fileUploadForm').addEventListener('submit', e => {
      e.preventDefault();
      const file = document.getElementById('fileInput').files[0];
      const formData = new FormData();
      formData.append('file', file);
      fetch('/protocol/system/upload', { method: 'POST', body: formData })
        .then(r => r.json()).then(d => {
          document.getElementById('fileUploadResponse').textContent = JSON.stringify(d, null, 2);
        }).catch(err => {
          document.getElementById('fileUploadResponse').textContent = '错误: ' + err;
        });
    });

    // 数据导出
    document.getElementById('exportButton').addEventListener('click', () => {
      fetch('/protocol/logManage/export', { cache: 'no-store' })
        .then(async (r) => {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          const blob = await r.blob();

          // 提取文件名
          const cd = r.headers.get('Content-Disposition');
          let filename = 'system.log';
          if (cd && cd.includes('filename=')) {
            filename = cd.split('filename=')[1].replace(/"/g, '');
          }

          // 创建下载链接
          const url = URL.createObjectURL(blob);
          const link = document.createElement('a');
          link.href = url;
          link.download = filename;
          document.body.appendChild(link);
          link.click();
          link.remove();
          URL.revokeObjectURL(url);
        })
        .catch(err => alert('获取数据时出错: ' + err));
    });


    // 设备升级
    document.getElementById('deviceUpgradeForm').addEventListener('submit', e => {
      e.preventDefault();
      const file = document.getElementById('upgradeFile').files[0];
      if (!file) return alert('请选择文件！');
      const formData = new FormData();
      formData.append('file', file);
      fetch(`/protocol/system/upgrade?totalSize=${file.size}`, { method: 'POST', body: formData })
        .then(r => {
          if (!r.ok) throw new Error('服务器返回错误：' + r.status);
          document.getElementById('upgradeResponse').textContent = '上传完成！';
        }).catch(err => {
          document.getElementById('upgradeResponse').textContent = '错误: ' + err.message;
        });
    });

    // GET 请求
    document.getElementById('getRequestForm').addEventListener('submit', e => {
      e.preventDefault();
      const url = document.getElementById('getUrl').value;
      fetch(url).then(r => r.text()).then(d => {
        document.getElementById('getResponse').textContent = d;
      }).catch(err => {
        document.getElementById('getResponse').textContent = '错误: ' + err;
      });
    });

    // POST 请求
    document.getElementById('postRequestForm').addEventListener('submit', e => {
      e.preventDefault();
      const url = document.getElementById('postUrl').value;
      const data = document.getElementById('postData').value;
      fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: data })
        .then(r => r.json()).then(d => {
          document.getElementById('postResponse').textContent = JSON.stringify(d, null, 2);
        }).catch(err => {
          document.getElementById('postResponse').textContent = '错误: ' + err;
        });
    });

    // CPU/MEM 柱状图
    const ctx = document.getElementById('cpuMemChart').getContext('2d');
    const cpuMemChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: ['CPU', '内存'],
        datasets: [{
          label: '占用率 (%)',
          data: [0, 0],
          backgroundColor: ["#734bd6", "#e27f95"],
          barPercentage: 0.7,        // 单个柱子的宽度比例
          categoryPercentage: 0.8    // 整个类别的宽度比例
        }]
      },
      options: {
        scales: {
          x: {
            ticks: { color: "#ccc" },
            grid: { color: "rgba(255,255,255,0.1)" }
          },
          y: {
            beginAtZero: true,
            max: 100,
            ticks: { color: "#ccc" },
            grid: { color: "rgba(255,255,255,0.1)" }
          }
        },
        responsive: true,
        plugins: {
          legend: { display: false },
          datalabels: {
            color: "#fff",       // 显示的文字颜色
            anchor: "end",       // 锚点（柱子顶端）
            align: "end",        // 文字对齐到柱子外面
            offset: -8,         // 负值让文字移到柱子顶部上方
            formatter: (val) => val + "%"   // 显示百分号
          }
        }
      },
      plugins: [ChartDataLabels]  // 注册插件
    });

    // 最近24小时CPU折线图
    const cpuLineCtx = document.getElementById('cpuLineChart').getContext('2d');
    const cpuLineChart = new Chart(cpuLineCtx, {
      type: 'line',
      data: {
      datasets: [{
                  label: 'CPU %',
                  borderColor: '#3b82f6',
                  backgroundColor: 'rgba(59,130,246,0.2)',
                  fill: true,
                  tension: 0.2,
                  data: [], // 不用提前填充，实时滚动
                  pointRadius: 0.001,      // 点半径，改小点
                  pointHoverRadius: 6,   // 鼠标悬停时点的大小
                  pointHitRadius: 40,       // 👈 大范围检测（重要！）
              }]
          },
          options: {
              responsive: true,
              animation: false,
              interaction: {
                //mode: 'nearest',
                mode: 'index',
                intersect: false
              },
              scales: {
                  x: {
                      type: 'realtime',
                      realtime: {
                          duration: 6 * 60 * 60 * 1000, // 最近6小时
                          refresh:  5 * 60 * 1000,       // 每分钟更新一次
                          delay: 0,                      // 延迟显示
                          pause: false,
                          //frameRate: 30, // 图表刷新帧率
                          //scrollX: true,  // 👈 让图往右增长而不是左滑
                          onRefresh: function(chart) {
                              //反转时间
                              const now = Date.now();

                              chart.data.datasets[0].data.push({
                                  //x: Date.now(),
                                  x: now,
                                  y: currentCPUValue,
                                  //displayTime: new Date(now).toLocaleTimeString()
                              });
                              // 保留最近24小时的数据
                              const cutoff = now - 6*60*60*1000;
                              chart.data.datasets[0].data = chart.data.datasets[0].data.filter(p => p.x >= cutoff);
                              // 反转时间轴方向：让最旧的数据在左，当前时间在右
                              const min = cutoff;
                              const max = now;
                              chart.options.scales.x.realtime.min = min;
                              chart.options.scales.x.realtime.max = max;
                              // 保存到 localStorage
                              localStorage.setItem('cpuData', JSON.stringify(chart.data.datasets[0].data));
                            }
                      },
                      time: {
                        //unit: 'minute', // 👈 强制以分钟为刻度单位
                        unit: 'hour', // 👈 强制以分钟为刻度单位
                        stepSize: 1,   // 👈 每10分钟一个刻度（可调成 1、5、15 等）
                        displayFormats: {
                          minute: 'HH:mm'  // 显示小时:分钟
                        },
                        tooltipFormat: 'MMM d, HH:mm'
                      },
                      ticks: {
                        color: '#ccc',
                        maxTicksLimit: 6,   // 限制显示6个刻度
                        autoSkip: true,
                        maxRotation: 0,
                        minRotation: 0
                      },
                      title: { display: true, text: '时间', color: '#ccc' },
                      reverse: true  // 反转 x 轴
                  },
                  y: {
                    beginAtZero: true,
                    suggestedMin: 0,
                    suggestedMax: 100,
                    title: { display: true, text: 'CPU %', color: '#ccc' },
                    ticks: { color: '#ccc' }
                  }
              },
              plugins: {
                legend: { display: false },
                datalabels: { display: false }, //禁止在点上显示坐标值
                    tooltip: {
                      enabled: true,
                      mode: 'nearest',
                      intersect: false,
                      callbacks: {
                        title: function(context) {
                            // 从 context[0].parsed.x 获取时间戳
                            const timestamp = context[0].parsed.x;
                            return new Date(timestamp).toLocaleString();
                        },
                        label: function(context) {
                            // 从 context.parsed.y 获取值
                            return `CPU: ${context.parsed.y.toFixed(2)}%`;
                        }
                      }
                    }
              }
          }
      });
    // 页面加载时恢复数据
    const savedData = JSON.parse(localStorage.getItem('cpuData') || '[]');
    cpuLineChart.data.datasets[0].data = savedData;
    cpuLineChart.update();

    // 监听保存按钮的点击事件
    document.getElementById('saveBtn').addEventListener('click', function() {
      // 获取 SSID 和密码的输入值
      const ssid = document.getElementById('ssid').value;
      const psk = document.getElementById('password').value;

      // 校验 SSID 和密码是否填写
      if (!ssid || !psk) {
        return alert('请填写 SSID 和密码！');
      }

      // 构造要发送的 JSON 数据
      const data = {
        data: {
          ssid: ssid,
          psk: psk
        }
      };

      // 发送 POST 请求到 /protocol/wifi/config
      fetch('/protocol/wifi/config', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json' // 设置请求头为 JSON 格式
        },
        body: JSON.stringify(data) // 将数据转换为 JSON 字符串
      })
        .then(response => {
          if (!response.ok) {
            throw new Error('服务器返回错误：' + response.status);
          }
          return response.json(); // 解析 JSON 响应
        })
        .then(responseData => {
          // 处理响应结果
          document.getElementById('upgradeResponse').textContent = '配置已保存！';
        })
        .catch(error => {
          // 错误处理
          document.getElementById('upgradeResponse').textContent = '错误: ' + error.message;
        });
    });



