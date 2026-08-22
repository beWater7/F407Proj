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
    if (id === 'wifi' && typeof loadWifiForm === 'function') {
      loadWifiForm();
    }
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
  // 更新系统信息：天气与 CPU/MEM/uptime 解耦，缺天气不影响其它字段
	function updateSystem() {
	  fetch('/protocol/system/info')
		.then(r => r.json())
    .then(d => {
      if (!d || !d.data) {
        throw new Error("数据缺失");
      }

      const data = d.data;

      if (data.uptime) {
        document.getElementById('upTime').textContent = '运行时间: ' + data.uptime;
      }
      if (data.systime) {
        document.getElementById('systemTime').textContent = '系统时间: ' + data.systime;
      }

      // 天气可选：空字符串 / 无字段时显示占位，不抛错
      const weatherEl = document.getElementById('weather');
      const tempEl = document.getElementById('temperature');
      if (weatherEl && tempEl) {
        const weather = (typeof data.weather === 'string') ? data.weather.trim() : '';
        const hasWeather = weather.length > 0;
        const hasTemp = typeof data.temperature === 'number';

        if (hasWeather) {
          const weatherEmojiMap = {
            '晴': '☀️',
            '雨': '🌧️',
            '雪': '❄️',
            '多云': '☁️',
            '雷阵雨': '⛈️'
          };
          let emoji = '☁️';
          for (const key in weatherEmojiMap) {
            if (weather.includes(key)) {
              emoji = weatherEmojiMap[key];
              break;
            }
          }
          weatherEl.textContent = `${emoji} ${weather}`;
          tempEl.textContent = hasTemp ? `${data.temperature}°C` : `--°C`;
        } else {
          weatherEl.textContent = '☁️ --';
          tempEl.textContent = '--°C';
        }
      }

      // CPU/MEM 独立更新（temperature===0 / weather==="" 时不再被短路）
      if (typeof data.cpu_usage === 'number' && typeof data.mem_usage === 'number') {
        if (cpuMemChart) {
          cpuMemChart.data.datasets[0].data = [data.cpu_usage, data.mem_usage];
          cpuMemChart.update();
        }
        currentCPUValue = data.cpu_usage;
      }
    })
    .catch(err => {
      console.error('获取系统数据错误:', err);
      document.getElementById('upTime').textContent = '运行时间: ...';
      document.getElementById('systemTime').textContent = '系统时间: ...';
    });
	}

	// 定时更新
	setInterval(updateSystem, 5000);
	updateSystem();


    // 文件上传 / 设备升级：带进度条的 POST
    function fmtBytes(n) {
      if (n < 1024) return n + ' B';
      if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
      return (n / 1048576).toFixed(2) + ' MB';
    }

    function setProgress(barEl, textEl, loaded, total, phase) {
      const pct = total ? Math.min(100, Math.round((loaded * 100) / total)) : 0;
      if (barEl) barEl.style.width = pct + '%';
      if (textEl) {
        if (phase === 'process') {
          textEl.textContent = '上传完成，设备处理中…';
        } else {
          textEl.textContent = pct + '%  (' + fmtBytes(loaded) + ' / ' + fmtBytes(total) + ')';
        }
      }
    }

    function postWithProgress(url, body, contentType, onProgress) {
      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', url);
        if (contentType) {
          xhr.setRequestHeader('Content-Type', contentType);
        }
        xhr.timeout = 10 * 60 * 1000;
        xhr.upload.onprogress = function (e) {
          if (e.lengthComputable && onProgress) {
            onProgress(e.loaded, e.total, 'upload');
          }
        };
        xhr.upload.onload = function () {
          if (onProgress) {
            const total = (body && body.size) ? body.size : 0;
            onProgress(total, total, 'process');
          }
        };
        xhr.onload = function () {
          if (xhr.status >= 200 && xhr.status < 300) {
            resolve({ status: xhr.status, text: xhr.responseText });
          } else {
            reject(new Error('HTTP ' + xhr.status + ': ' + String(xhr.responseText || '').slice(0, 200)));
          }
        };
        xhr.onerror = function () { reject(new Error('网络错误')); };
        xhr.ontimeout = function () { reject(new Error('上传超时')); };
        xhr.send(body);
      });
    }

    // 文件上传
    document.getElementById('fileUploadForm').addEventListener('submit', e => {
      e.preventDefault();
      const file = document.getElementById('fileInput').files[0];
      if (!file) return;
      const wrap = document.getElementById('fileUploadProgressWrap');
      const bar = document.getElementById('fileUploadProgressBar');
      const text = document.getElementById('fileUploadProgressText');
      const respEl = document.getElementById('fileUploadResponse');
      const btn = document.getElementById('fileUploadBtn');
      const formData = new FormData();
      formData.append('file', file);
      wrap.classList.remove('hidden');
      bar.style.width = '0%';
      setProgress(bar, text, 0, file.size, 'upload');
      respEl.textContent = '上传中… ' + file.name + ' (' + fmtBytes(file.size) + ')';
      btn.disabled = true;
      postWithProgress('/protocol/system/upload', formData, null, function (loaded, total, phase) {
        setProgress(bar, text, loaded, total, phase);
      }).then(function (res) {
        try {
          respEl.textContent = JSON.stringify(JSON.parse(res.text), null, 2);
        } catch (err) {
          respEl.textContent = res.text || '上传完成';
        }
        setProgress(bar, text, file.size, file.size, 'upload');
      }).catch(function (err) {
        respEl.textContent = '错误: ' + err.message;
      }).finally(function () {
        btn.disabled = false;
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


    // 设备升级：上传 genUpgBin.py 生成的 upg.bin（含 APP+web）
    document.getElementById('deviceUpgradeForm').addEventListener('submit', e => {
      e.preventDefault();
      const file = document.getElementById('upgradeFile').files[0];
      if (!file) return alert('请选择文件！');
      if (!/\.bin$/i.test(file.name)) {
        if (!confirm('建议上传 upg.bin（由 genUpgBin.py 生成）。仍要继续？')) return;
      }
      const respEl = document.getElementById('upgradeResponse');
      const wrap = document.getElementById('upgradeProgressWrap');
      const bar = document.getElementById('upgradeProgressBar');
      const text = document.getElementById('upgradeProgressText');
      const btn = document.getElementById('upgradeBtn');
      wrap.classList.remove('hidden');
      bar.style.width = '0%';
      setProgress(bar, text, 0, file.size, 'upload');
      respEl.textContent = '上传中… ' + file.name + ' (' + fmtBytes(file.size) + ')';
      btn.disabled = true;

      postWithProgress('/protocol/system/upgrade?totalSize=' + file.size, file,
        'application/octet-stream',
        function (loaded, total, phase) {
          setProgress(bar, text, loaded, total, phase);
          if (phase === 'process') {
            respEl.textContent = '上传完成，设备正在写入 Flash…';
          }
        })
        .then(function (res) {
          let msg = '上传完成，设备即将重启';
          try {
            const j = JSON.parse(res.text);
            if (j.status && j.status !== 200) {
              throw new Error(j.errorMsg || JSON.stringify(j));
            }
            if (j.errorMsg) msg = j.errorMsg === 'ok' ? msg : j.errorMsg;
          } catch (parseErr) {
            if (parseErr.message && parseErr.message.indexOf('JSON') < 0) throw parseErr;
          }
          setProgress(bar, text, file.size, file.size, 'upload');
          respEl.textContent = msg;
        }).catch(function (err) {
          respEl.textContent = '错误: ' + err.message;
        }).finally(function () {
          btn.disabled = false;
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
      const ssid = document.getElementById('ssid').value.trim();
      const psk = document.getElementById('password').value;
      const msg = document.getElementById('wifiSaveMsg');

      if (!ssid) {
        return alert('请填写 SSID！');
      }
      if (ssid.length > 32) {
        return alert('SSID 最长 32 字节');
      }
      if (psk.length > 0 && psk.length < 8) {
        return alert('Wi-Fi 密码至少 8 位（开放网络请留空）');
      }
      if (psk.length > 63) {
        return alert('密码最长 63 字节');
      }

      const data = {
        data: {
          ssid: ssid,
          psk: psk,
          password: psk
        }
      };

      if (msg) {
        msg.textContent = '正在下发...';
      }
      fetch('/protocol/wifi/config', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
      })
        .then(response => response.text().then(text => ({ ok: response.ok, status: response.status, text })))
        .then(({ ok, status, text }) => {
          let parsed = null;
          try { parsed = text ? JSON.parse(text) : null; } catch (e) { parsed = null; }
          if (!ok || (parsed && parsed.status && parsed.status !== 200)) {
            const em = (parsed && parsed.errorMsg) ? parsed.errorMsg : ('HTTP ' + status);
            throw new Error(em);
          }
          if (msg) {
            msg.textContent = '配置已受理，正在连接 AP...';
          }
          const poll = (left) => {
            if (left <= 0) {
              if (msg) msg.textContent = '等待超时，请看串口日志';
              return;
            }
            fetch('/protocol/wifi/config')
              .then(r => r.json())
              .then(j => {
                const d = (j && j.data) ? j.data : {};
                const st = d.state || '';
                if (st === 'ok') {
                  if (msg) msg.textContent = '已连接 AP: ' + (d.ssid || '');
                  return;
                }
                if (st === 'fail') {
                  if (msg) msg.textContent = '连接失败: ' + (d.error || '');
                  return;
                }
                if (msg) msg.textContent = '状态: ' + (st || 'pending');
                setTimeout(() => poll(left - 1), 1000);
              })
              .catch(() => setTimeout(() => poll(left - 1), 1000));
          };
          poll(30);
        })
        .catch(error => {
          if (msg) {
            msg.textContent = '错误: ' + error.message;
          }
        });
    });



