// ESP32-S3 Car Controller Service Worker
const CACHE_NAME = 'esp32-car-v1.0.0';
const urlsToCache = [
  './',
  './gamepad_interface.html',
  './manifest.json',
  './favicon.ico'
];

// 安装事件 - 缓存资源
self.addEventListener('install', event => {
  console.log('Service Worker: 安装中...');
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(cache => {
        console.log('Service Worker: 缓存文件');
        return cache.addAll(urlsToCache);
      })
      .then(() => {
        console.log('Service Worker: 安装完成');
        return self.skipWaiting();
      })
  );
});

// 激活事件 - 清理旧缓存
self.addEventListener('activate', event => {
  console.log('Service Worker: 激活中...');
  event.waitUntil(
    caches.keys().then(cacheNames => {
      return Promise.all(
        cacheNames.map(cacheName => {
          if (cacheName !== CACHE_NAME) {
            console.log('Service Worker: 删除旧缓存', cacheName);
            return caches.delete(cacheName);
          }
        })
      );
    }).then(() => {
      console.log('Service Worker: 激活完成');
      return self.clients.claim();
    })
  );
});

// 拦截网络请求
self.addEventListener('fetch', event => {
  // 跳过非GET请求和API调用
  if (event.request.method !== 'GET' || 
      event.request.url.includes('/api') ||
      event.request.url.includes('192.168.')) {
    return;
  }

  event.respondWith(
    caches.match(event.request)
      .then(response => {
        // 如果缓存中有，直接返回
        if (response) {
          console.log('Service Worker: 从缓存返回', event.request.url);
          return response;
        }

        // 否则从网络获取
        console.log('Service Worker: 从网络获取', event.request.url);
        return fetch(event.request).then(response => {
          // 检查响应是否有效
          if (!response || response.status !== 200 || response.type !== 'basic') {
            return response;
          }

          // 克隆响应用于缓存
          const responseToCache = response.clone();
          caches.open(CACHE_NAME)
            .then(cache => {
              cache.put(event.request, responseToCache);
            });

          return response;
        }).catch(() => {
          // 网络失败时的后备方案
          if (event.request.destination === 'document') {
            return caches.match('./gamepad_interface.html');
          }
        });
      })
  );
});

// 处理推送通知（可选）
self.addEventListener('push', event => {
  if (event.data) {
    const options = {
      body: event.data.text(),
      icon: './icon-192x192.png',
      badge: './icon-72x72.png',
      vibrate: [100, 50, 100],
      data: {
        dateOfArrival: Date.now(),
        primaryKey: 1
      }
    };

    event.waitUntil(
      self.registration.showNotification('ESP32小车通知', options)
    );
  }
});

// 处理通知点击
self.addEventListener('notificationclick', event => {
  console.log('通知被点击');
  event.notification.close();

  event.waitUntil(
    clients.openWindow('./gamepad_interface.html')
  );
});

// 处理消息
self.addEventListener('message', event => {
  if (event.data && event.data.type === 'SKIP_WAITING') {
    self.skipWaiting();
  }
});