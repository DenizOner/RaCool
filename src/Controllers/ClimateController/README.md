# Fan kontrolü ve sıcaklık kaynakları

`ClimateController` ölçümleri toplar; saf `TemperatureSources.h` kaynakları çözer ve fan kararını verir. `FanCommands_Dispatch` OLED, CLI ve Zigbee'nin semantik ayar girişidir. ConfigurationManager tam aday doğrulaması, alan allowlist'i, persistence ve rollback'i sahiplenir. Harici ölçüm girişi `IClimateController.SetExternalTemperature` üzerinden yalnız RAM'e gider.

Nesneye duyarlı mod kapalı fanı efektif nesne sıcaklığı **Fan Açılma Sıcaklığı** değerine ulaştığında açar; açık fanı **Fan Kapanma Sıcaklığı** değerine indiğinde kapatır. Ortama duyarlı mod aynı histerezisi **efektif nesne − efektif ortam** farkıyla uygular. **Fan Açılma Sıcaklık Farkı** ve **Fan Kapanma Sıcaklık Farkı** signed −9.0…+9.0 °C, 0.1 derece çözünürlük ve ON > OFF koşuluna sahiptir. Varsayılan +2.0/+1.0'dır. Aradaki bölgede fan durumu korunur; mevcut debounce ve minimum anahtarlama süresi ayrıca uygulanır.

Nesne ve ortam kaynakları bağımsız Dahili / Harici 1 / Harici 2 / Harici 3 seçimleridir. Dahili nesne/ortam MLX90614'ün ilgili ölçümüdür. Her harici slot value/valid/last_update bilgisi taşır; age okuma anında hesaplanır. Yeni measurement injection aynı değeri gönderse de heartbeat'tir; GET veya cached telemetri freshness yenilemez. 15 dakika güncellenmeyen slot stale olur. Seçilen slot boş/eskiyse ilgili geçerli dahili ölçüm fallback olur; harici yeni veriyle geri gelirse seçim değiştirilmeden yeniden effective olur.

Otomatik kontrolün gerekli kaynağı ve dahili fallback birlikte geçersizse fan **fail-safe AÇIK** yapılır. Nesne modunda geçersiz ortam tek başına bu sonucu gerektirmez; fark modu her iki ölçümü gerektirir. CPU koruması daha yüksek öncelikli bağımsız katmandır. Geçerli ölçüm geri geldiğinde normal histerezis/debounce yeniden devralır. Yazılım durum/GPIO kanıtı gerçek fan dönüşü veya röle kontağı gözlemi değildir.

## CLI

```text
CLIMATE MODE OBJECT_SENSITIVE
CLIMATE MODE AMBIENT_DELTA
CLIMATE MODE ON
CLIMATE MODE OFF
CLIMATE SET HIGH 35
CLIMATE SET LOW 28
CLIMATE SOURCE OBJECT 2
CLIMATE SOURCE AMBIENT 1
CLIMATE DELTA -1 -2
CLIMATE SOURCES
CLIMATE EXTERNAL 1 21.5
CLIMATE EXTERNAL 1 CLEAR
CLIMATE ACTION
CLIMATE SAVE
```

CLI ayarları mevcut convention gereği `CLIMATE SAVE` ile kalıcılaşır; OLED ve Zigbee ayar yazımları checked persistence kullanır. `DELTA` iki eşiği tek transaction'da değiştirir. `EXTERNAL` developer/debug measurement girişidir, SAVE kapsamına girmez. `SOURCES` configured/effective kaynak, normal/fallback/invalid durumu, geçerlilik, değer ve age; üç slotun waiting_for_update/valid/stale durumunu gösterir.

`EXTERNAL <slot> CLEAR` RAM slotunu boşaltır; MQTT karşılığı ilgili external property'ye `null` yazmaktır. `CLIMATE RESET` mod/kaynak/eşiklerin bütün fan defaults'unu ortak dispatcher ile tek control transaction'da uygular; calibration ikinci block'tur ve kısmi hata açıkça bildirilir. `CLIMATE ACTION` diagnostic istek sequence/pending/active/result bilgilerini gösterir. Röle pulse/auto-off/self-test komutları mevcut Climate worker'a admission verir; safety veya stop bunları iptal edebilir.

Legacy `ClimateControllerConfig.hysteresisDeadband` yalnız nesne modunun isteğe bağlı internal API modifier'ıdır; varsayılan sıfırdır. Fark modunun inclusive signed sınırlarını değiştirmez. `STATUS` içindeki Sensor Stale dahili IR acquisition'ı anlatır; etkin harici kaynak geçerliyken bu flag tek başına fan fail-safe kanıtı değildir. Efektif sağlık için `SOURCES` kullanılır. Z2M read-only kaynak diagnostikleri GET ile güncel attribute snapshot'ını verir; MQTT cache tek başına yeni acquisition veya unsolicited delivery kanıtı değildir.

## Persistence ve dış sözleşme

ControlConfigs V2 32 byte, version offset 28'dir. V1 24 byte/version offset 20 ve ilk 18 byte alanları açık migration ile korunur; eski iki bool nesne/manuel açık/manuel kapalı anlamını kaybetmez. Yeni strateji Dahili kaynaklar ve +2/+1 varsayılanıyla açılır. Gelecek/uyumsuz version veya uzunluk reddedilir. Migration tüm aday doğrulandıktan sonra commit edilir; SDK/commit hatasında runtime geri alınır. Namespace'ler arası atomiklik vaat edilmez. Slot sıcaklıkları, geçerlilik ve zaman damgaları NVS'ye yazılmaz; gerçek restart sonrası waiting_for_update başlar.

Aktif endpoint EP1 `climate`'tır. Eski HA `isi_dagitma_fani` unique/entity kimlikleri yalnız compatibility mapping içinde korunur. `fan_operating_mode_climate` seçeneği `Otomatik` yerine `Nesneye duyarlı` olmuştur; `Ortama duyarlı` eklenmiştir. Bu onaylı option rename MQTT/HA automation değerlerinin güncellenmesini gerektirir. CLI eski `AUTO` yerine `OBJECT_SENSITIVE` kullanır. LED/OLED automatic modlarının isimleri bu değişiklikten etkilenmez.

EP1 `hvacUserInterfaceCfg` yeni contract:

| ID | İçerik | ZCL türü |
|---|---|---|
| 0x8050 | mode: object=0, manual on=1, manual off=2, ambient delta=3 | UINT8 |
| 0x8051–52 | configured object/ambient source: internal=0, ext1–3=1–3 | UINT8 |
| 0x8053–54 | delta ON/OFF, signed tenths | INT16 |
| 0x8055–57 | external measurement input, signed hundredths | INT16 |
| 0x8058–5A | effective object/ambient/difference, signed hundredths | INT16, read only |
| 0x805B–5C | effective source | UINT8, read only |
| 0x805D–5E | source status: normal=0, fallback=1, invalid=2 | UINT8, read only |
| 0x805F–61 | external slot state: waiting=0, valid=1, stale=2 | UINT8, read only |

Geçersiz INT16 ölçüm `INT16_MIN` sentinel'ıdır; kontrol algoritması bunu sıcaklık olarak kullanmaz. Mevcut standart CoolingOnly SystemMode 0=off, 1=nesne, 3=manuel açık interoperability rotasıdır; yeni fark stratejisi için standart dışı SystemMode değeri uydurulmamıştır. Mod kontrolü yeni custom attribute'a taşınır. Mevcut absolute setpoint attribute/property rotaları korunur; kullanıcı label'ları Fan Açılma/Kapanma Sıcaklığı olur.

HA measurement aktarımı firmware dışında yapılır. [Heartbeat örneği](/tools/home-assistant/racool-external-temperature.yaml) state change, HA start ve 5 dakika heartbeat kullanır. Örnek entity/topic yerine gerçek cihazla ilişkili değerler seçilmelidir; unavailable/unknown kaynak aktarılmaz. Harici verinin tekrar başlaması fallback'i otomatik kaldırır.

## Kanıt sınırları

Native testler saf histerezis, negatif eşikler, eskime/fallback/recovery ve owner field validation/migration hata yollarını kapsar. Gerçek reboot, 15 dakikalık timeout, ZCL/HA discovery ve OLED/encoder/fan gözlemi ayrıca canlı kabul gerektirir. Tamamlanmamış canlı kabul native PASS ile tamamlanmış sayılmaz.

Canlı kaynak kabulü: `.venv/Scripts/python.exe test/Controllers/ClimateController/diagnostic/verify_fan_sources.py --port ... --output ... --stale-window`. Bu helper ayarları/boş slot baseline'ını yazmadan önce saklar; kaynak eşleşmeleri, signed histerezis, persistence ve gerçek15 dakika eskimeyi sınar; finally SAVE ve fresh software boot ile geri yükler. `live_zigbee.mjs read-fan-sources` bütün18 custom attribute için correlated raw ZCL okur; `verify-fan-sources` signed delta ve üç external input'u restore ederek test eder. Tek seri owner kuralı geçerlidir. Ölçülmüş sonuçlar DEV_VERIFICATION_REPORT.md'dedir.
