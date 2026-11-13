# Test dizini düzeni

Modül testleri `src/` ağacını izler: örneğin `src/Hardware/BuiltinLED/` için testler `test/Hardware/BuiltinLED/` altındadır. Mevcut modül testlerinin düzenlenmesi bütün kaynak modüllerinin test edildiği anlamına gelmez; kapsam kayıtları MODULE_AUDIT.md içindedir.

- Modülün canlı yardımcıları `diagnostic/`, donanım hedef senaryoları `target/`, modüle özel taklitleri `mocks/` altına yerleşir.
- `Infrastructure/` ortak SDK taklitleri, test desteği, entegrasyonlar ve çalıştırıcı kabul testleri içindir; üretim modülü değildir.
- `test_target_smoke/` ve `test_target_rgb/` PlatformIO'nun keşfetmesi gereken giriş dosyalarını ve modül hedef senaryolarına ince yönlendirmeleri barındırır. Gerçek senaryolar ilgili modülün `target/` dizinindedir.
- Kök çalıştırıcılar ve kayıt dosyaları ortak giriş sözleşmesidir. `build/` ve Python önbellekleri üretilmiş, yok sayılan çıktılardır.

`node test/check_test_layout.mjs` modül klasörlerinin kaynak karşılığını kontrol eder. `node test/check_test_inventory.mjs` paket/kasa kayıtlarını ayrı doğrular. İki kontrol de başarılı olmadan düzenleme tamamlanmış sayılmaz.
