# Güvenlik politikası

## Desteklenen sürümler

RaCool henüz fiziksel ürün kabulünden geçmiş, yayımlanmış bir **kararlı firmware sürümü** ilan etmemiştir. `resources/product-version.json` içindeki geçici sürüm tabanı veya GitHub Actions'ın ürettiği `.bin` dosyaları, desteklenen kararlı bir dağıtım anlamına gelmez.

Gelecekte desteklenen sürümler yalnız doğrulanmış GitHub Release ve ilgili donanım/OTA kabul kayıtları üzerinden ilan edilecektir.

## Güvenlik açığını özel olarak bildirme

Gizli bilgi sızıntısı, kimlik doğrulama, Zigbee ağı, NVS, watchdog, fan/röle veya elektriksel güvenlik sorunu fark ederseniz:

1. Depodaki **Security → Advisories → Report a vulnerability** özel bildirim yolunu kullanın (**özel güvenlik bildirimi** depo sahibi tarafından etkinleştirilmiş olmalıdır).
2. Bu yol görünmüyorsa özel bir iletişim kanalı sağlanmasını depo sahibinden isteyin; **zafiyetin ayrıntısını veya gizli bilgileri GitHub Issues, PR, Discussions ya da herkese açık yorumlara yazmayın**.
3. Raporda etkilenen sürüm/commit, beklenen ve gözlenen davranış, güvenli yeniden üretim adımları ve olası etkiyi belirtin; gerçek anahtar, token, parola, tam özel cihaz kimliği veya hassas log eklemeyin.

Özellikle fan/röle/şebeke ve Zigbee ağ davranışını değiştirebilecek kanıtlar gerçek cihaz üzerinde izinsiz denenmemelidir. Aşırı riskli açıklamalar kamuya yayımlanmadan önce gizli kanaldan ele alınır.

## Müdahale ve açıklama

Erişim bilgisi sızıntısında yetkili sahibi önce ilgili anahtarı/kimlik bilgisini **iptal eder veya yeniler**, erişim kayıtlarını kontrol eder ve etkisini araştırır. Geçmişteki veriyi yalnız silmek sızdırılmış bilgiyi geçersiz kılmaz. Git geçmişini yeniden yazma, cihaz/NVS/ağ sıfırlama ve fiziksel müdahale ayrı Ürün Sahibi kararı gerektirir.

Yama; ilgili testler, bağımsız denetim ve gerekiyorsa gerçek cihaz kabulüyle doğrulandıktan sonra güvenlik bildirimi güncellenir.
