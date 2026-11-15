## Amaç ve bağlantılı kayıt
<!-- Kısaca kullanıcı davranışını ve değişikliğin nedenini belirtin. -->
Bağlantılı Issue: Refs #N
<!-- Fiziksel kabul kriterleri açıkken Issue'yu otomatik kapatan anahtar sözcükler kullanmayın. -->

## Kapsam
- [ ] Hata düzeltmesi
- [ ] Yeni işlev
- [ ] Test / CI / güvenlik iyileştirmesi
- [ ] Dokümantasyon

**Kullanıcıya etkisi / değişen sözleşme:**

**Riskler, negatif senaryolar ve geri dönüş:**

## Otomatik doğrulamalar (ilgili son HEAD ve base)
- [ ] `node test/check_test_layout.mjs`
- [ ] `node test/check_test_inventory.mjs`
- [ ] `node test/run_helper_tests.mjs`
- [ ] `node test/run_native_tests.mjs`
- [ ] `pio run -e esp32-c6-devkitc-1` (derleme 0 hata / 0 uyarı)
- [ ] `Firmware Build & Host Native Tests (Linux)` — PASS
- [ ] `Layout & Helper Suites (Windows)` — PASS
- [ ] Gerekli negatif, yarış, yeniden açılış ve geri yükleme testleri
- [ ] Doğrulanamayan / çalıştırılmayan testler **NOT TESTED** olarak belirtildi

**CI çalışması ve son HEAD bağlantısı:**

## Bağımsız denetim ve gerçek cihaz kabulü
- [ ] Ayrı salt-okunur denetçi son ilgili farkı inceledi; PR yorumunda `PASS / FAIL / BLOCKED` ve gerekçe var
- [ ] Denetçinin yorumu, GitHub'ın resmî PR approval işlemi olarak gösterilmiyor
- [ ] Gerekli gerçek cihaz, röle, OLED, encoder, NVS yeniden açılış ve Zigbee RF kabulü ayrıca kanıtlandı **veya açıkça BLOCKED / NOT TESTED**
- [ ] Gizli bilgi / yerel ayar / özel cihaz kimliği eklenmedi
- [ ] İlgili `CODEBASE.md` / modül belgeleri, sözleşme değiştiyse güncellendi

**Donanım gözlemi (gerekliyse, sonuç ve sorumlu):**
