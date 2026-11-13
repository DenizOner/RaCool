# Canlı menü kontrolleri

Üretim CLI'sı için `menu auto-exit status --machine` kalıcı hareketsizlikte çıkış ayarını okur. `menu auto-exit off --machine` menüyü Daima Açık ekran modunda açık tutar; `menu auto-exit on --machine` yapılandırılmış OLED hareketsizlik süresini tekrar uygular. Varsayılan açıktır. Ekranın Otomatik moddaki kapanışı ve ana sayfadan uyanışı bu seçenekle devre dışı bırakılmaz.

Mutasyon öncesi SYSTEM CONFIG çıktısındaki tüm alanları ve mevcut menü durumunu kaydedin. Başkasının açık taslağını/işlemini kapatmayın. Tek seri sahibi kullanın. Açık/kapalı sınamasında aynı menüyü OLED süresinden uzun hareketsiz bırakın; durum okumasının kendisi encoder hareketi değildir. CLI kaydı, gerçek encoder/optik kabul yerine geçmez.

Normal sınama sonunda özgün değeri geri yükleyip okuyun. Kullanıcının fiziksel gözlem oturumunda ise ekran Daima Açık ve auto-exit kapalı tutulur; TASK_CHECKLIST.md içindeki oturum sonu görevi ikisini birlikte Otomatik/açık durumuna döndürür. Firmware yeniden başlasa da kapalı seçim korunur.

Mevcut yardımcılar: verify_led_menu.py, verify_telemetry_menu.py, verify_menu_registration.py. Kayıt ve geri yükleme sonuçları makine çıktısı ve taze owner alanlarıyla doğrulanır.
