# Zigbee rapor reddi tanılaması

`analyze_tasklet_watchdog.py --log CAPTURE --elf EXACT_ELF --nm NM --objdump OBJDUMP`
tek bir Zigbee_main/IDLE watchdog register dökümünü ilgili RISC-V ELF ile
karşılaştırır. `EXACT_ELF`, yakalanan firmware'e ait olmalıdır; araç bu
eşleşmeyi tek başına doğrulayamaz. Gerekli sembol aralıkları veya tanınan SDK
komut dizisi bulunmazsa analiz reddedilir. Aynı adresin current/next olarak
görünmesi yalnız o snapshot'ın kendi bağlantısıyla uyumunu gösterir; döngünün
nasıl oluştuğunu veya önceki çökmelerle ilişkisini kanıtlamaz. Araç salt
okunurdur ve cihazdaki firmware'e dahil edilmez.

`live_zigbee.mjs` bağlantı kesilince bekleyen bridge isteklerini başarısız
sonlandırır, durum beklemelerini kapatır ve uzun işlemin temizlik blokları
tamamlandıktan sonra sonuç JSON'unu yazar. Altı rapor aralığından biri seçilmiş
`verify` koşusunda, trafik, ek değişim veya yeniden yapılandırma yoksa, kontrol
ve özellik eşleşmesi doğrulanır; ZCL ile doğrulanmış yardımcı
değeri aynı IEEE cihazına yeni bağlantı, taze korelasyonlu ZCL okuma ve tek
geri yazma/son okuma ile koşullu geri yüklenir. Belirsiz teslim veya yabancı
değer otomatik yazılmaz. Diğer ayarlar ve köprü log seviyesi için ayrı bağlantı
ile otomatik kurtarma yoktur; başarısız sonuçta sahipliği ve taze değerleri
bağımsız doğrulayın. Belirsiz mutasyonu körlemesine yinelemeyin. Okuma ile
yazma arasında başka bir aktöre karşı atomik kilit yoktur.

`node test/Communication/Zigbee/diagnostic/live_zigbee.mjs read-interval-wire
report_interval_radiator_s` seçilen cihazda raporlama aralığını korelasyonlu raw
ZCL yanıtıyla salt okunur sorgular. Aynı kip `ambient`, `processor`, `health`,
`memory` ve `uptime` adlarıyla altı aralığı destekler. Bu yanıt MQTT durum
önbelleğinden farklıdır; fiziksel çıktı veya RF raporu teslimi anlamına gelmez.

`RACOOL_ZIGBEE_REPORT_DIAGNOSTIC=ON`, yalnız geçici firmware'e
`report_trace.c` dosyasını ve on linker wrapper'ını ekler. Varsayılan OFF'tur.
PlatformIO `esp32-c6-zigbee-report-trace` ortamı bu seçeneği yalnız kendi
geçici imajında açar; doğrulama sonrası üretim ortamı geri yüklenmelidir.
Bağlandığı SDK sürümünün ELF çağrı yollarına bağlıdır; 2.0.5 yükseltmesinde imzaları,
link edilmiş çağrı noktalarını ve gerçek hook sayaçlarını yeniden doğrulayın.
Özel SDK yapıları okunmaz/yazılmaz; opak pointer ve dönüşler aynen iletilir.
Wrapper'lar SDK davranışını veya rapor sonucunu değiştirmez.

`RPTTRACE` kaydı üst rapor hatası ile paket hazırlama/gönderim/APS dönüşlerini
eşler. `*_calls=0`, o alt yolun izlenmediğini belirtir; sonucu başarı diye
yorumlamayın. Kayıt yalnız raporun sahibi görevden toplanır. İlk 12 rapor,
`0x805e` raporları ve bütün reddedilen istekler yazılır; alt hook içinde log,
allocation veya bekleme yoktur. Native `ZigbeeReportTrace` paketi aktarım ve
sahiplik davranışını sınar; SDK/hardware kanıtı değildir.
`clone_calls` ve `clone_failures` yalnız aynı görevde tek rapor çağrısı içinde
sarmalanan klonları sayar; `pool_start` ve `pool_end` eşzamanlı, kısmi havuz
snapshot'larıdır. Başka görevlerin ve sarmalanmayan çağrıların etkisi dışlanmaz.

`RPTALLOC`, reddedilen istekte en son 64 `zmsg_alloc` çağrısından sınırlı bir
örnek özetler. Adresler build'in ELF dosyası ile çözümlenmelidir. Bu kayıt
yalnız yakın çağrı yerleridir; hâlâ ayrılmış tamponların sahiplerini, toplam
havuz kullanımını veya kesin nedenini göstermez. Atomik sabit tampon dışında
ayırma, bekleme ve SDK iç alanlarına erişim yoktur; kancanın zamanlama etkisi
ayrıca değerlendirilmelidir.

`RPTEVENT` ilk rapor reddinde son en fazla 256 havuz olayını kaydeder: `M`
başarılı pool-1 ayırma, `X` başarısız ayırma, `F` bırakma, `A` çağrı yeri
bilinen zmsg ayırma, `Z` başarısız zmsg ayırma ve `G` kayıp kayıttır.
`B`, `C` ve `D` sırasıyla pool-0 veri tamponunun başarılı ayırma, başarısız
ayırma ve bırakma olaylarıdır; bu kayıt tamponu belirli mesaja bağlamaz.
`RPTPOOL` MCU açılışından beri kancalarda görülen canlı/tepe tampon sayısını
ve eşleşmeyen bırakmaları gösterir. Sayaçlar eşzamanlıdır ve anlık görüntü
tam atomik değildir; SDK toplu yeniden başlatması veya sarmalanmayan çağrılar
sayacı geçersiz kılabilir. Bu nedenle sahiplik veya sızıntı kanıtı değildir.
`RPTDRAIN` ilk rapor reddinden sonra, başka rapor çağrısı olursa en sık iki
saniyede bir canlı ve tepe sayıyı örnekler. Rapor çağrısı olmadan örnek almaz;
bu nedenle sürekli izleme veya tampon sahipliği kanıtı olarak yorumlanmaz.
`RPTBIND` yalnız geçici imajda açılıştan 40 saniye sonra mevcut report worker
üzerinden SDK'nın herkese açık ZDO yönetim isteğini cihazın kendi kısa adresine
gönderir. Sayfalar sınırlıdır; callback veriyi kopyalar, yazdırma worker'da
yapılır. SDK dokümanı uzak cihazı tarif eder; kendine istek yanıtlanmazsa bu
yalnız başarısız/eksik tanılamadır. İstek ağ trafiği oluşturur, binding yazmaz.
`python test/Communication/Zigbee/diagnostic/analyze_bind_pages.py LOG` tüm
sayfaların sırasını, sayısını ve indekslerini doğrular; sayfalar arasında tablo
değişebilir ve çıktıda hedef adresler bulunmaz.
`RPTORIGIN`, izlenen canlı tamponları ayrıldıkları `zmsg_alloc` çağrı yerine
göre özetler. Bu, mevcut tampon sahibini veya tahsisten sonraki devri göstermez;
ilk başarılı rapordan sonra `RPTSTART` işaretiyle bir defa da alınır. İlk
rapor başarısızsa hata snapshot'ı kullanılır; yazdırma zamanlamayı etkileyebilir.
ZCL paket hazırlama wrapper'ından geçen iç içe ayırmalarda bu wrapper'ın üst
çağrı yeri kullanılır; mevcut SDK/ELF çağrı noktaları yeniden doğrulanmalıdır.
İç ZCL report wrapper'ı, bağlanan SDK otomatik raporlama ve doğrudan API
çağrılarının üst çağrı yerlerini ayırır; sarmalanmayan yollar bilinmeyendir.
Doğrudan SDK iç çağrıları, ISR bağlamı ve dolu bağlam tabloları bilinmeyendir.
Mesaj klonlama wrapper'ı yalnız ELF'te bağlantısı doğrulanan harici çağrıları
ilişkilendirir; aynı nesne dosyasındaki doğrudan çağrıları sarmayabilir.
Snapshot eşzamanlıdır; overflow, unmatched ve unknown sayaçlarını inceleyin.
Sabit tablo ve tarama ek CPU/RAM kullanır ve hata zamanlamasını değiştirebilir.
`python test/Communication/Zigbee/diagnostic/analyze_pool_events.py LOG` eksik,
yinelenmiş veya kayıp kayıtları reddeder. Pencere öncesi ayırmalar ve linker'ın
sarmadığı çağrılar bilinmez; gözlem bir sızıntı kanıtı değildir. İlk hatadan
sonraki seri döküm zamanlamayı etkileyebilir. Üretimde bu kancalar yoktur.

`RACOOL_ZIGBEE_REPORT_LEGACY_CONTEXT_DIAGNOSTIC=ON` yalnız negatif kontrol
içindir: eski raporlama işini ortak ESP timer bağlamına döndürür. Bu seçenek
ana rapor tanılaması olmadan configure edilemez ve üretimde mutlaka OFF'tur.
`task`/`priority` alanları eski `esp_timer` ile normal `Zigbee_report` bağlamını
ayırt eder. Tanılama sırasında gözlenen eski bağlam hatasını üretim sonucu
saymayın; aynı ayarlarla normal bağlama dönüşü ve teslimi ayrıca doğrulayın.

Güncel tüm yapılandırma alanlarını ve bridge log seviyesini mutasyon öncesi kaydedin. Tek seri
sahibiyle gerçek açılış/SDK restart izleyin; raw ZCL okuması ve unsolicited
attributeReport teslimini ayrı doğrulayın. Binding tablosu/kimlik bilgisi
silerek veya özel alan değiştirerek hata üretmeyin. Sınırlı temiz açılışları
aralıklı kök nedenin kanıtı saymayın.

`verify_zigbee_lifecycle.py` her servis restartından önce menü ve sistemin
boşta olduğunu doğrular. Zigbee worker, son SDK okuru ayrıldıktan ve ana döngü çıktıktan sonra
SDK deinit çağrısını bir kez yapar. İkinci fiziksel 2.0.5 denemesinde
`ezb_af_free_device_desc` çağrısının **deinit öncesine alınması** da çöktü;
ELF izi `esp_zigbee_deinit → ezb_core_deinit → af_desc_deinit →
af_free_device_desc` zincirini gösterdi. AF ağacı **başarılı kayıt sonrasında** SDK mülkiyetindedir; deinit kendisi
temizlediği için uygulama ayrıca free çağırmaz. SDK 2.0.5 arşivindeki
`af_desc.c.obj` incelemesine göre kayıt öncesi veya başarısız kayıt sonrasında
SDK küresel listesinde kök tutulmaz: bu hata yollarında uygulama ağacı
SDK deinit öncesinde **bir kez** serbest bırakır. Sonrasında uygulama handle'ı
geçersiz kılınır. Bu sözleşme SDK sürümüne bağlıdır; yükseltmede yeniden
doğrulanmalıdır. Yerel native/target taklitleri bu sahipliği modeller ancak
gerçek SDK bellek yönetimini kanıtlamaz. Üç ardışık yeniden başlatma,
ağ/ayar kalıcılığı ve EP1 RF teslimi gerçek kartta doğrulanmalıdır. Sonucu belirsiz bir restartta tek kontrollü kurtarma
resetinden sonra gerçek boot ve reporting değerlerini denetler. Geri yükleme
sırasındaki SDK hata satırları saklanır ve test başarısız kalır; Developer Mode
tercihi mümkünse checked readback ile geri alınır. Fatal hata veya yabancı
sahiplik altında başarı varsayılmaz.

İsteğe bağlı `--observe-restart-selftest`, yalnız restart yanıtı sırasında
Zigbee self-test hatası görülürse sonraki döngülerin gözlenmesini sağlar. Hata
JSON'da korunur ve sonuç başarısızdır; başka servis hataları ve fatal çıktılar
reddedilir. `--observe-report-backpressure` da yalnız belirli yeniden
denenebilir bellek baskısını gözlemler ve koşuyu başarısız bırakır. Bu kipler
sağlıklı yaşam döngüsü kabulü veya RF teslimi kanıtı değildir.

Sonunda seçeneği OFF yapın; cache, ELF'te wrapper yokluğu, sıfır warning/error
normal upload, gerçek boot ve ayar geri yüklemesini doğrulayın. Fiziksel
oturumdaki Daima Açık modu son gözleme kadar korunur.

`observe-fan-sources` için `RACOOL_FAN_SOURCE_OBSERVATION_MS` varsayılanı
22000, izin verilen aralık 22000–60000 ms'dir. Reset/başlatma süresini de
kapsayan senaryoda pencereyi yeterince uzun seçin. Başlangıç/bitiş raw ZCL
işaretleyicileri ve unsolicited rapor filtreleri değişmez; üç karar ölçümünün
rapor koşulu korunur. Geçersiz süre log seviyesi değişiminden önce reddedilir;
başarısız pencerelerin rapor satırları da kanıt dosyasında saklanır.
