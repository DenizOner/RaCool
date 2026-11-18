# Test dizini düzeni

Modül testleri `src/` ağacını izler: örneğin `src/Hardware/BuiltinLED/` için testler `test/Hardware/BuiltinLED/` altındadır. Mevcut modül testlerinin düzenlenmesi bütün kaynak modüllerinin test edildiği anlamına gelmez; kapsam kayıtları MODULE_AUDIT.md içindedir.

- Modülün canlı yardımcıları `diagnostic/`, donanım hedef senaryoları `target/`, modüle özel taklitleri `mocks/` altına yerleşir.
- `Infrastructure/` ortak SDK taklitleri, test desteği, entegrasyonlar ve çalıştırıcı kabul testleri içindir; üretim modülü değildir.
- `test_target_smoke/` ve `test_target_rgb/` PlatformIO'nun keşfetmesi gereken giriş dosyalarını ve modül hedef senaryolarına ince yönlendirmeleri barındırır. Gerçek senaryolar ilgili modülün `target/` dizinindedir.
- Kök çalıştırıcılar ve kayıt dosyaları ortak giriş sözleşmesidir. `build/` ve Python önbellekleri üretilmiş, yok sayılan çıktılardır.

`node test/check_test_layout.mjs` modül klasörlerinin kaynak karşılığını kontrol eder. `node test/check_test_inventory.mjs` paket/kasa kayıtlarını ayrı doğrular. İki kontrol de başarılı olmadan düzenleme tamamlanmış sayılmaz.

Zigbee'nin donanım gerektirmeyen MQTT kurtarma testleri iki ayrı helper paketinde tutulur: `test/Communication/Zigbee/test_live_recovery.mjs` erişim izni ve altı ZCL rapor aralığı için 26 vakayı, `test/Communication/Zigbee/test_bridge_log_recovery.mjs` ortak bridge log düzeyinin güvenli geri yüklenmesi için 20 vakayı yürütür. `helper_suites.json` dosyası bu 46 vakayı iki bağımsız süreçte, mevcut dosya başına zaman aşımı ve başarısızlık kuralları değiştirilmeden kaydeder. Sahte TCP/MQTT testlerinin geçmesi canlı Zigbee RF veya cihaz kabulü değildir.
