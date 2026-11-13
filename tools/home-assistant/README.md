# RaCool çift uçlu bölge kartı

## Aranabilir ayar grupları

`racool-settings-card.js` dosyasını HA `www/` dizinine kopyalayın ve
`/local/racool-settings-card.js?v=1` kaynağını JavaScript modülü olarak kaydedin.
Panel üreticisi bu üçüncü kaynağı kullanır. Genel durum sürekli görünür; fan
kontrolü başlangıçta açıktır, diğer gruplar kapalıdır. Klavye ile Tab/Enter
kullanılarak gruplar açılabilir. Arama başlık ve kullanıcı etiketlerinde çalışır,
kapalı grupları da bulup açar; arama temizlenince önceki açıklık durumuna döner.
Fan moduna bağlı koşullar aramayla kaldırılmaz. Her cihazın kendi görünümü,
arama alanı ve kartları vardır; varlık kimlikleri veya MQTT sözleşmesi değişmez.
Bu düzen HA kontrol paneline aittir; Zigbee2MQTT Exposes sayfasına özel başlık
ya da sahte sensör eklemez. Test: `node --test test/Infrastructure/test_settings_card.mjs`.

`racool-zone-card.js` dış JavaScript bağımlılığı kullanmaz. Home Assistant'ın
`www/` dizinine kopyalayın ve Ayarlar → Kontrol panelleri → Kaynaklar altında
`/local/racool-zone-card.js?v=1` adresini **JavaScript modülü** olarak ekleyin.
Dosyayı değiştirdiğinizde sürüm sorgusunu artırın ve tarayıcıyı yenileyin.

Kartı panelin YAML düzenleyicisine ekleyin. Aşağıdaki örnek varlık adlarını
kurulumunuzdaki gerçek Zigbee2MQTT `number` varlıklarıyla değiştirin:

```yaml
type: custom:racool-zone-card
title: Bölge aralıkları
zones:
  - label: Soğuk
    low: number.racool_rgb_soguk_alt_sinir
    high: number.racool_rgb_soguk_ust_sinir
    min: 7
    max: 25
  - label: Ilık
    low: number.racool_rgb_ilik_alt_sinir
    high: number.racool_rgb_ilik_ust_sinir
    min: 26
    max: 44
  - label: Sıcak
    low: number.racool_rgb_sicak_alt_sinir
    high: number.racool_rgb_sicak_ust_sinir
    min: 45
    max: 59
```

Her çizgide iki tutamak vardır. Dokunma/fare veya Tab ve yön tuşlarıyla
ayarlanır; alt ve üst sınır arasında en az 1 °C bırakılır. Her hareket yalnız
ilgili `number.set_value` servisini çağırır. Geçersiz/kullanılamayan varlıkta
kontrol kapanır. Bekleyen değişiklik güncel varlık durumuyla eşleşene kadar
aynı bölge tekrar yazılmaz; gönderim hatası veya 10 saniyelik durum timeout
açıkça gösterilir. HA durum eşleşmesi fiziksel ölçüm veya korele ZCL yanıtı
olarak sunulmaz; otomatik canlı kabul ayrıca gerçek ZCL okuması yapar.

Test: `node --test test/Infrastructure/test_zone_card.mjs`.
Gerçek sınır okuması: `node test/Communication/Zigbee/diagnostic/live_zigbee.mjs read-ranges`.
Canlı testten önce altı başlangıç sınırını kaydedin; her koşulda geri yükleyin
ve korele ZCL yanıtıyla doğrulayın. Mevcut paneli değiştirirken yedek alın;
diğer kart ve kaynakları koruyun.


## Home Assistant renk çemberi

`racool-color-card.js` dosyasını aynı www dizinine kopyalayın ve `/local/racool-color-card.js?v=1` kaynağını JavaScript modülü olarak kaydedin. Kart HA'nın kendi renk çemberini yükler; beş sahte lamba üretmez.

```yaml
type: custom:racool-color-card
title: Bölge renkleri
command_topic: zigbee2mqtt/YOUR_DEVICE/set
zones:
  - label: Soğuk
    entity: sensor.your_cold_color_xy
    property: cold_region_color_xy_thermal_indicator
```

En fazla beş bölge eklenir: cold, warm, hot, danger, freeze. Her entity kurulumunuzdaki gerçek XY sensor olmalı. Mevcut HA kimlikleri İngilizce sözleşmeye geçişte korunur; eski Türkçe entity kimlikleri normaldir, MQTT property adları İngilizcedir. Renk seçiminde X/Y tek payload ile gönderilir ve taze HA durum cevabı beklenir. Parlaklık ayrı kontrol edilir. Test: `node --test test/Infrastructure/test_color_card.mjs`.


## Bir veya birden fazla RaCool için taşınabilir panel

`devices.example.json` dosyasını kendi yerel yapılandırmanıza kopyalayın. Her cihaz için kullanıcıya görünen ad, gerçek Zigbee IEEE adresi ve Zigbee2MQTT set topic'i girin. Topic'i Z2M cihaz bilgi sayfasından alın; cihaz adı değişirse yapılandırmayı yeniden üretin. Parola/token eklemeyin. Bir cihaz için bir görünüm, birden fazla cihaz için üstte cihaz görünümü sekmeleri üretilir; hiçbir kullanıcı adına, entity prefix'ine, IP adresine veya porta sabit bağımlılık yoktur.

Node bağımlılıklarını proje kökünde `npm ci` ile kurun. Home Assistant entity registry'nin salt okunur JSON dışa aktarımını kullanın veya HA yapılandırma diskindeki `.storage/core.entity_registry` dosyasının yolunu verin. Dosyayı düzenlemeyin; özel registry ve üretilen paneli GitHub'a yüklemeyin.

```powershell
node tools/home-assistant/generate-dashboard.mjs devices.local.json /path/to/core.entity_registry dashboard.local.json
```

Üretici kimlikleri IEEE + mevcut/legacy discovery unique_id üzerinden eşler. Eksik, devre dışı, çift veya başka cihazla karışmış eşleşmelerde panel üretimini reddeder; tahmini entity ID yazmaz. Açık MQTT topic yapılandırmasının seçtiğiniz IEEE cihazına ait olması kurulum sorumluluğudur; topic/IEEE eşleşmesi ağ üzerinden otomatik kanıtlanmış sayılmaz.

HA'da yeni boş bir kontrol paneli oluşturun. Üç JavaScript kaynağını yukarıdaki gibi kaydedin. Mevcut paneli değiştirecekseniz ham YAML'ını yedekleyin; çıktıyı Ham yapılandırma düzenleyicisine yapıştırıp kaydedin. JSON geçerli YAML biçimidir. Yeniden üretim elle eklenen kartları otomatik birleştirmez; özel kartlarınızı koruyarak aktarın.

Fan eşikleri aynı `racool-zone-card` üzerinde 20–99 °C aralığında solda kapanma ve sağda açılma olarak gösterilir. Tutamaklar birbirini geçemez; 1 °C minimum açıklık korunur. Cihazda daha hassas eşikler varsa panel bunları okur; slider tam derece seçim yapar. Renk property durumları standard CIE XY 0–1 koordinatlarıdır; firmware'e geçişte 16 bit ölçekte doğrulanır. Eski integer XY yazmaları uyumluluk için kabul edilir. CLI/OLED ayarları değişmez.

Test: `node --test test/Infrastructure/test_dashboard_generator.mjs test/Infrastructure/test_zone_card.mjs test/Infrastructure/test_color_card.mjs`.

Güncel panelde Donma sınırı, üç bölge aralığının önünde; Tehlike sınırı
sonundadır. `racool-zone-card` isteğe bağlı `before`/`after` HA kartlarını aynı
kutuda taşır. `step: 0.1` ve `min_gap: 0.1` ortam farkı için ondalık çift slider
sağlar; diğer aralıkların varsayılan adımı/açıklığı 1 °C olarak kalır. Nesne
modunda kaynak ve eşikler birlikte, ortam modunda yalnız ortam kaynağı/fark
eşikleri görünür. Tekrarlanan fark ölçümü tanılamada tutulur. LED Kapalı veya
ekran Daima Açık modlarında kullanılmayan kontroller gizlenir; mod seçicileri
görünür kalır. Dahili başlangıç animasyonu kendi LED oynatıcısını kullanır.

Panel fan moduna göre değişir: Nesneye duyarlı mod mutlak sıcaklık eşiklerini, Ortama duyarlı mod ortam kaynağını ve sıcaklık farkı eşiklerini gösterir. Kapalı/Sürekli Açık modlarında otomatik ayarlar gizlenir; CPU koruması değişmez. Harici ölçüm girişleri yalnız otomatik modda seçilen ilgili kaynak için görünür. Kalibrasyon yerel sıcaklıkların diğer tüketicilerini de etkilediğinden ayrı ve sürekli görünür; ayrıntılı kaynak tanılaması en sondadır. HA düzenleme modunda koşullu kartlar düzenlenebilmek için birlikte görünür; dinamik davranışı Bitti ile normal görünümde kontrol edin.

Kurulumda HA cihaz sayfasından varsayılan devre dışı olabilen “Zigbee Sinyal Kalitesi” varlığını etkinleştirin. Varlığın eklenmesi ve registry kaydının güncellenmesinden sonra paneli üretin. Üretici devre dışı varlıklara tahmini bağlantı oluşturmaz.

HA/Z2M arayüzünde her bölge için yalnız renk seçicisi vardır; hazır renk listeleri kaldırılmıştır. Başlıklar “RGB Soğuk Bölge Rengi” gibi görünür. Seçiciler eski hazır renk ayarlarının sırasındadır. CLI ve OLED renk düzenleme seçenekleri korunur.
