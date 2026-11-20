#include "intl.h"
#include <fribidi/fribidi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
static unsigned language;
static bool music;
static const char *names[INTL_LANGUAGE_COUNT]={"English","العربية","Français","Español","Deutsch","Português","Italiano","Русский","Türkçe"};
typedef struct {const char *text[INTL_LANGUAGE_COUNT];} Message;
static const Message messages[]={
    {{"Settings","الإعدادات","Paramètres","Ajustes","Einstellungen","Configurações","Impostazioni","Настройки","Ayarlar"}},
    {{"Close","إغلاق","Fermer","Cerrar","Schließen","Fechar","Chiudi","Закрыть","Kapat"}},
    {{"View","عرض","Vue","Vista","Ansicht","Vista","Vista","Вид","Görünüm"}},
    {{"Tiles","بلاطات","Tuiles","Mapas","Kacheln","Mapas","Mappe","Тайлы","Karolar"}},
    {{"Data","بيانات","Données","Datos","Daten","Dados","Dati","Данные","Veri"}},
    {{"Explore","استكشاف","Explorer","Explorar","Erkunden","Explorar","Esplora","Обзор","Keşfet"}},
    {{"Text","نص","Texte","Texto","Text","Texto","Testo","Текст","Metin"}},
    {{"More","المزيد","Plus","Más","Mehr","Mais","Altro","Ещё","Diğer"}},
    {{"Language","اللغة","Langue","Idioma","Sprache","Idioma","Lingua","Язык","Dil"}},
    {{"Music","الموسيقى","Musique","Música","Musik","Música","Musica","Музыка","Müzik"}},
    {{"ON","تشغيل","OUI","SÍ","AN","SIM","SÌ","ВКЛ","AÇIK"}},
    {{"OFF","إيقاف","NON","NO","AUS","NÃO","NO","ВЫКЛ","KAPALI"}},
    {{"On","تشغيل","Oui","Sí","An","Sim","Sì","Вкл","Açık"}},
    {{"Off","إيقاف","Non","No","Aus","Não","No","Выкл","Kapalı"}},
    {{"Normal","عادي","Normal","Normal","Normal","Normal","Normale","Обычный","Normal"}},
    {{"Large","كبير","Grand","Grande","Groß","Grande","Grande","Крупный","Büyük"}},
    {{"Extra Large","كبير جدا","Très grand","Muy grande","Sehr groß","Muito grande","Molto grande","Огромный","Çok büyük"}},
    {{"Regular","عادي","Normal","Normal","Normal","Normal","Normale","Обычный","Normal"}},
    {{"Medium","متوسط","Moyen","Medio","Mittel","Médio","Medio","Средний","Orta"}},
    {{"Bold","عريض","Gras","Negrita","Fett","Negrito","Grassetto","Жирный","Kalın"}},
    {{"Text size","حجم النص","Taille du texte","Tamaño del texto","Textgröße","Tamanho do texto","Dimensione testo","Размер текста","Metin boyutu"}},
    {{"Text weight","سمك النص","Graisse du texte","Grosor del texto","Schriftstärke","Peso do texto","Spessore testo","Толщина текста","Yazı kalınlığı"}},
    {{"High contrast","تباين عال","Contraste élevé","Alto contraste","Hoher Kontrast","Alto contraste","Contrasto alto","Контраст","Yüksek kontrast"}},
    {{"World globe","الكرة الأرضية","Globe terrestre","Globo terráqueo","Weltkugel","Globo terrestre","Globo terrestre","Земной шар","Dünya küresi"}},
    {{"3D terrain","تضاريس ثلاثية الأبعاد","Relief 3D","Relieve 3D","3D-Gelände","Terreno 3D","Terreno 3D","Рельеф 3D","3B arazi"}},
    {{"Street View Explorer","استكشاف الشوارع","Explorer les rues","Explorar calles","Straßen erkunden","Explorar ruas","Esplora strade","Панорамы улиц","Sokakları keşfet"}},
    {{"Play StreetGuess","العب تخمين المكان","Jouer à StreetGuess","Jugar StreetGuess","StreetGuess spielen","Jogar StreetGuess","Gioca a StreetGuess","Играть в StreetGuess","StreetGuess oyna"}},
    {{"Search","بحث","Rechercher","Buscar","Suchen","Buscar","Cerca","Поиск","Ara"}},
    {{"Favorites","المفضلة","Favoris","Favoritos","Favoriten","Favoritos","Preferiti","Избранное","Favoriler"}},
    {{"Recents","الأماكن الأخيرة","Récents","Recientes","Zuletzt","Recentes","Recenti","Недавние","Son yerler"}},
    {{"Route","المسار","Itinéraire","Ruta","Route","Rota","Percorso","Маршрут","Rota"}},
    {{"Identify","تحديد المكان","Identifier","Identificar","Erkennen","Identificar","Identifica","Определить","Tanımla"}},
    {{"Menu","القائمة","Menu","Menú","Menü","Menu","Menu","Меню","Menü"}},
    {{"Back","رجوع","Retour","Volver","Zurück","Voltar","Indietro","Назад","Geri"}},
    {{"Cancel","إلغاء","Annuler","Cancelar","Abbrechen","Cancelar","Annulla","Отмена","İptal"}},
    {{"Ready","جاهز","Prêt","Listo","Bereit","Pronto","Pronto","Готово","Hazır"}},
    {{"Street","شوارع","Rues","Calles","Straße","Ruas","Strade","Улицы","Sokak"}},
    {{"Satellite","قمر صناعي","Satellite","Satélite","Satellit","Satélite","Satellite","Спутник","Uydu"}},
    {{"Night mode","الوضع الليلي","Mode nuit","Modo nocturno","Nachtmodus","Modo noturno","Modalità notte","Ночной режим","Gece modu"}},
    {{"Debug overlay","معلومات التشخيص","Diagnostic","Diagnóstico","Diagnose","Diagnóstico","Diagnostica","Диагностика","Tanılama"}},
    {{"SD cache","ذاكرة البطاقة","Cache SD","Caché SD","SD-Cache","Cache SD","Cache SD","Кэш SD","SD önbelleği"}},
    {{"Clear SD","مسح البطاقة","Vider SD","Vaciar SD","SD leeren","Limpar SD","Svuota SD","Очистить SD","SD temizle"}},
    {{"Offline","دون اتصال","Hors ligne","Sin conexión","Offline","Offline","Offline","Без сети","Çevrimdışı"}},
    {{"regions","مناطق","Régions","Regiones","Regionen","Regiões","Regioni","Регионы","Bölgeler"}},
    {{"Slow","بطيء","Lent","Lento","Langsam","Lento","Lento","Медленно","Yavaş"}},
    {{"Fast","سريع","Rapide","Rápido","Schnell","Rápido","Veloce","Быстро","Hızlı"}},
    {{"Standard","قياسي","Standard","Estándar","Standard","Padrão","Standard","Стандарт","Standart"}},
    {{"Detail","تفاصيل","Détail","Detalle","Detail","Detalhe","Dettaglio","Детально","Ayrıntı"}},
    {{"Auto","تلقائي","Auto","Auto","Auto","Auto","Auto","Авто","Otomatik"}},
    {{"Low","منخفض","Faible","Bajo","Niedrig","Baixo","Basso","Низко","Düşük"}},
    {{"High","عال","Élevé","Alto","Hoch","Alto","Alto","Высоко","Yüksek"}},
    {{"Yes","نعم","Oui","Sí","Ja","Sim","Sì","Да","Evet"}},
    {{"No","لا","Non","No","Nein","Não","No","Нет","Hayır"}},
    {{"Classic","كلاسيكي","Classique","Clásico","Klassisch","Clássico","Classico","Классика","Klasik"}},
    {{"Daily Challenge","التحدي اليومي","Défi du jour","Reto diario","Tagesaufgabe","Desafio diário","Sfida giornaliera","Задание дня","Günlük görev"}},
    {{"Friend Challenge","تحدي الأصدقاء","Défi entre amis","Reto de amigos","Freundesaufgabe","Desafio de amigos","Sfida amici","Вызов другу","Arkadaş görevi"}},
    {{"Pass and Play","تبادل اللعب","Chacun son tour","Por turnos","Abwechselnd","Por turnos","A turni","По очереди","Sırayla oyna"}},
    {{"Country Streak","سلسلة البلدان","Série de pays","Racha de países","Länderserie","Série de países","Serie di paesi","Серия стран","Ülke serisi"}},
    {{"Surprise Me","فاجئني","Surprenez-moi","Sorpréndeme","Überrasche mich","Surpreenda-me","Sorprendimi","Удиви меня","Beni şaşırt"}},
    {{"World","العالم","Monde","Mundo","Welt","Mundo","Mondo","Мир","Dünya"}},
    {{"Europe","أوروبا","Europe","Europa","Europa","Europa","Europa","Европа","Avrupa"}},
    {{"Easy","سهل","Facile","Fácil","Einfach","Fácil","Facile","Легко","Kolay"}},
    {{"Expert","خبير","Expert","Experto","Experte","Especialista","Esperto","Эксперт","Uzman"}},
    {{"Controls and Earth quality","التحكم وجودة الأرض","Commandes et qualité","Controles y calidad","Steuerung und Qualität","Controles e qualidade","Comandi e qualità","Управление и качество","Kontroller ve kalite"}},
    {{"Readability, text size and weight","وضوح النص وحجمه وسمكه","Lisibilité et police","Legibilidad y fuente","Lesbarkeit und Schrift","Legibilidade e fonte","Leggibilità e caratteri","Читаемость и шрифт","Okunabilirlik ve yazı"}},
    {{"Style and live overlays","المظهر والمعلومات الحية","Style et informations","Estilo e información","Stil und Anzeigen","Estilo e informações","Stile e informazioni","Стиль и информация","Stil ve bilgiler"}},
    {{"Tile loading behavior","تحميل الخرائط","Chargement des cartes","Carga de mapas","Karten laden","Carregamento de mapas","Caricamento mappe","Загрузка карт","Harita yükleme"}},
    {{"Network and storage","الشبكة والتخزين","Réseau et stockage","Red y almacenamiento","Netzwerk und Speicher","Rede e armazenamento","Rete e memoria","Сеть и хранилище","Ağ ve depolama"}},
    {{"GPS source and server","مصدر الموقع والخادم","Source GPS et serveur","Fuente GPS y servidor","GPS-Quelle und Server","Fonte GPS e servidor","Sorgente GPS e server","Источник GPS и сервер","GPS kaynağı ve sunucu"}},
    {{"Globe, terrain and street-level photos","الأرض والتضاريس وصور الشوارع","Globe, relief et rues","Globo, relieve y calles","Globus, Gelände und Straßen","Globo, terreno e ruas","Globo, terreno e strade","Глобус, рельеф и улицы","Küre, arazi ve sokaklar"}},
    {{"No saved places","لا توجد أماكن محفوظة","Aucun lieu enregistré","No hay lugares guardados","Keine gespeicherten Orte","Nenhum local salvo","Nessun luogo salvato","Нет сохранённых мест","Kayıtlı yer yok"}},
    {{"Search results","نتائج البحث","Résultats","Resultados","Suchergebnisse","Resultados","Risultati","Результаты поиска","Arama sonuçları"}},
    {{"Map center","مركز الخريطة","Centre de la carte","Centro del mapa","Kartenmitte","Centro do mapa","Centro mappa","Центр карты","Harita merkezi"}},
    {{"Saved to Recents","تم الحفظ في الأماكن الأخيرة","Ajouté aux récents","Guardado en recientes","Zuletzt gespeichert","Salvo nos recentes","Salvato nei recenti","Сохранено в недавних","Son yerlere kaydedildi"}},
    {{"No saved regions yet","لا توجد مناطق محفوظة","Aucune région enregistrée","No hay regiones guardadas","Keine gespeicherten Regionen","Nenhuma região salva","Nessuna regione salvata","Нет сохранённых регионов","Kayıtlı bölge yok"}},
    {{"Language / Music","اللغة والموسيقى","Langue et musique","Idioma y música","Sprache und Musik","Idioma e música","Lingua e musica","Язык и музыка","Dil ve müzik"}},
    {{"Map controls speed","سرعة تحريك الخريطة","Vitesse de la carte","Velocidad del mapa","Kartengeschwindigkeit","Velocidade do mapa","Velocità mappa","Скорость карты","Harita hızı"}},
    {{"Circle Pad deadzone","المنطقة الميتة للعصا","Zone morte du pad","Zona muerta del pad","Stick-Totzone","Zona morta do pad","Zona morta pad","Мёртвая зона стика","Çubuk ölü bölgesi"}},
    {{"Invert gyro axes","عكس محاور الحركة","Inverser le gyroscope","Invertir giroscopio","Gyroachsen umkehren","Inverter giroscópio","Inverti giroscopio","Инверсия гироскопа","Jiroskopu ters çevir"}},
    {{"Panorama quality","جودة الصور البانورامية","Qualité panoramique","Calidad panorámica","Panoramaqualität","Qualidade panorâmica","Qualità panorama","Качество панорамы","Panorama kalitesi"}},
    {{"Terrain exaggeration","مبالغة ارتفاع التضاريس","Exagération du relief","Exageración del relieve","Geländeüberhöhung","Exagero do relevo","Esagerazione rilievo","Масштаб высот","Arazi yüksekliği"}},
    {{"Stereoscopic buildings","مبان مجسمة","Bâtiments en relief","Edificios en relieve","Stereoskopische Gebäude","Edifícios em relevo","Edifici stereoscopici","Стереоздания","Üç boyutlu binalar"}},
    {{"Globe detail","تفاصيل الكرة الأرضية","Détail du globe","Detalle del globo","Globusdetails","Detalhe do globo","Dettaglio globo","Детализация глобуса","Küre ayrıntısı"}},
    {{"StreetGuess locations","مواقع تخمين المكان","Lieux StreetGuess","Lugares StreetGuess","StreetGuess-Orte","Locais StreetGuess","Luoghi StreetGuess","Места StreetGuess","StreetGuess yerleri"}},
    {{"Control profile","ملف التحكم","Profil de commandes","Perfil de controles","Steuerungsprofil","Perfil de controles","Profilo comandi","Профиль управления","Kontrol profili"}},
    {{"Remap logical action","تغيير وظيفة الزر","Changer une action","Cambiar una acción","Aktion zuweisen","Alterar uma ação","Cambia azione","Переназначить действие","Eylemi değiştir"}},
    {{"Bind physical button","تعيين الزر","Associer un bouton","Asignar botón","Taste zuweisen","Atribuir botão","Assegna pulsante","Назначить кнопку","Düğme ata"}},
    {{"Reset this profile","إعادة ضبط الملف","Réinitialiser le profil","Restablecer perfil","Profil zurücksetzen","Redefinir perfil","Reimposta profilo","Сбросить профиль","Profili sıfırla"}},
    {{"Balanced","متوازن","Équilibré","Equilibrado","Ausgewogen","Equilibrado","Bilanciato","Сбалансированно","Dengeli"}},
    {{"Urban","حضري","Urbain","Urbano","Städtisch","Urbano","Urbano","Город","Kentsel"}},
    {{"Rural","ريفي","Rural","Rural","Ländlich","Rural","Rurale","Село","Kırsal"}},
    {{"Broad","واسع","Large","Amplio","Breit","Amplo","Ampio","Широко","Geniş"}},
    {{"GPS source","مصدر الموقع","Source GPS","Fuente GPS","GPS-Quelle","Fonte GPS","Sorgente GPS","Источник GPS","GPS kaynağı"}},
    {{"Client","عميل","Client","Cliente","Client","Cliente","Client","Клиент","İstemci"}},
    {{"Server","خادم","Serveur","Servidor","Server","Servidor","Server","Сервер","Sunucu"}},
    {{"PAUSED","متوقف مؤقتا","PAUSE","PAUSA","PAUSE","PAUSA","PAUSA","ПАУЗА","DURAKLATILDI"}},
    {{"GAME PAUSED","اللعبة متوقفة مؤقتا","JEU EN PAUSE","JUEGO EN PAUSA","SPIEL PAUSIERT","JOGO PAUSADO","GIOCO IN PAUSA","ИГРА НА ПАУЗЕ","OYUN DURAKLATILDI"}},
    {{"PLACE YOUR GUESS BELOW","حدد تخمينك أدناه","PLACEZ VOTRE RÉPONSE","COLOCA TU RESPUESTA","TIPP UNTEN PLATZIEREN","MARQUE SEU PALPITE","INDICA LA RISPOSTA","ОТМЕТЬТЕ МЕСТО ВНИЗУ","TAHMİNİNİ AŞAĞIYA KOY"}},
    {{"A geography game for 3DS","لعبة جغرافيا لجهاز 3DS","Un jeu de géographie sur 3DS","Un juego de geografía para 3DS","Ein Geografiespiel für 3DS","Um jogo de geografia para 3DS","Un gioco di geografia per 3DS","Игра о географии для 3DS","3DS için coğrafya oyunu"}},
    {{"1. Look at mystery Street View","1. انظر إلى صورة الشارع","1. Observez la rue mystère","1. Mira la calle misteriosa","1. Unbekannte Straße ansehen","1. Veja a rua misteriosa","1. Osserva la strada misteriosa","1. Изучите незнакомую улицу","1. Gizemli sokağa bak"}},
    {{"2. Tap the map to place a pin","2. المس الخريطة لتحديد المكان","2. Placez un repère sur la carte","2. Toca el mapa para marcar","2. Einen Punkt auf der Karte setzen","2. Toque no mapa para marcar","2. Tocca la mappa per segnare","2. Поставьте метку на карте","2. Haritaya dokun ve işaretle"}},
    {{"3. Press A to GUESS","3. اضغط A لتأكيد التخمين","3. Appuyez sur A pour répondre","3. Pulsa A para responder","3. Mit A den Tipp abgeben","3. Aperte A para responder","3. Premi A per rispondere","3. Нажмите A для ответа","3. Tahmin için A düğmesine bas"}},
    {{"RETURN TO START","العودة إلى البداية","RETOUR AU DÉPART","VOLVER AL INICIO","ZURÜCK ZUM START","VOLTAR AO INÍCIO","TORNA ALL'INIZIO","ВЕРНУТЬСЯ В НАЧАЛО","BAŞLANGICA DÖN"}},
    {{"PLAY STREETGUESS","العب تخمين المكان","JOUER À STREETGUESS","JUGAR STREETGUESS","STREETGUESS SPIELEN","JOGAR STREETGUESS","GIOCA A STREETGUESS","ИГРАТЬ В STREETGUESS","STREETGUESS OYNA"}},
    {{"RECORDS","السجلات","RECORDS","RÉCORDS","REKORDE","RECORDES","RECORD","РЕКОРДЫ","REKORLAR"}},
    {{"ENTER FRIEND CODE","أدخل رمز الصديق","CODE D'AMI","CÓDIGO DE AMIGO","FREUNDESCODE EINGEBEN","CÓDIGO DE AMIGO","CODICE AMICO","КОД ДРУГА","ARKADAŞ KODU GİR"}},
    {{"UTC DAILY RECORDS","السجلات اليومية UTC","RECORDS QUOTIDIENS UTC","RÉCORDS DIARIOS UTC","TAGESREKORDE UTC","RECORDES DIÁRIOS UTC","RECORD GIORNALIERI UTC","РЕКОРДЫ ДНЯ UTC","GÜNLÜK REKORLAR UTC"}},
    {{"Tap to change region","المس لتغيير المنطقة","Touchez pour changer de région","Toca para cambiar región","Tippen für andere Region","Toque para mudar região","Tocca per cambiare regione","Нажмите для смены региона","Bölgeyi değiştirmek için dokun"}},
    {{"Tap to change moving rules","المس لتغيير قواعد الحركة","Touchez pour changer les règles","Toca para cambiar reglas","Tippen für Bewegungsregeln","Toque para mudar regras","Tocca per cambiare regole","Нажмите для смены правил","Hareket kurallarını değiştir"}},
    {{"Tap to change timer","المس لتغيير المؤقت","Touchez pour changer le temps","Toca para cambiar tiempo","Tippen für anderes Zeitlimit","Toque para mudar tempo","Tocca per cambiare tempo","Нажмите для смены таймера","Süreyi değiştirmek için dokun"}},
    {{"Time: Unlimited","الوقت: غير محدود","Temps : illimité","Tiempo: ilimitado","Zeit: unbegrenzt","Tempo: ilimitado","Tempo: illimitato","Время: без ограничения","Süre: sınırsız"}},
    {{"Mode: %s","الوضع: %s","Mode : %s","Modo: %s","Modus: %s","Modo: %s","Modalità: %s","Режим: %s","Mod: %s"}},
    {{"Difficulty: %s","الصعوبة: %s","Difficulté : %s","Dificultad: %s","Schwierigkeit: %s","Dificuldade: %s","Difficoltà: %s","Сложность: %s","Zorluk: %s"}},
    {{"Players: %u","اللاعبون: %u","Joueurs : %u","Jugadores: %u","Spieler: %u","Jogadores: %u","Giocatori: %u","Игроки: %u","Oyuncular: %u"}},
    {{"Map: %s","الخريطة: %s","Carte : %s","Mapa: %s","Karte: %s","Mapa: %s","Mappa: %s","Карта: %s","Harita: %s"}},
    {{"Rules: %s","القواعد: %s","Règles : %s","Reglas: %s","Regeln: %s","Regras: %s","Regole: %s","Правила: %s","Kurallar: %s"}},
    {{"Time: %u seconds","الوقت: %u ثانية","Temps : %u secondes","Tiempo: %u segundos","Zeit: %u Sekunden","Tempo: %u segundos","Tempo: %u secondi","Время: %u секунд","Süre: %u saniye"}},
    {{"Round %u of %u","الجولة %u من %u","Manche %u sur %u","Ronda %u de %u","Runde %u von %u","Rodada %u de %u","Round %u di %u","Раунд %u из %u","Tur %u / %u"}},
    {{"Round %u: %u points","الجولة %u: %u نقطة","Manche %u : %u points","Ronda %u: %u puntos","Runde %u: %u Punkte","Rodada %u: %u pontos","Round %u: %u punti","Раунд %u: %u очков","Tur %u: %u puan"}},
    {{"A to start  |  Y how to play","A للبدء | Y طريقة اللعب","A commencer | Y aide","A empezar | Y ayuda","A starten | Y Anleitung","A começar | Y ajuda","A inizia | Y aiuto","A начать | Y помощь","A başla | Y yardım"}},
    {{"A play   Y help   B exit to Maps","A العب Y مساعدة B الخريطة","A jouer Y aide B carte","A jugar Y ayuda B mapa","A spielen Y Hilfe B Karte","A jogar Y ajuda B mapa","A gioca Y aiuto B mappa","A играть Y помощь B карта","A oyna Y yardım B harita"}},
    {{"A  Resume round","A استئناف الجولة","A Reprendre","A Continuar","A Fortsetzen","A Continuar","A Riprendi","A Продолжить","A Devam et"}},
    {{"B  Back to game menu","B العودة لقائمة اللعبة","B Menu du jeu","B Menú del juego","B Spielmenü","B Menu do jogo","B Menu di gioco","B Меню игры","B Oyun menüsü"}},
    {{"A resume   B quit to menu","A استئناف B القائمة","A reprendre B menu","A continuar B menú","A fortsetzen B Menü","A continuar B menu","A riprendi B menu","A продолжить B меню","A devam B menü"}},
    {{"A: Try again","A: حاول مجددا","A : Réessayer","A: Reintentar","A: Erneut versuchen","A: Tentar novamente","A: Riprova","A: Повторить","A: Yeniden dene"}},
    {{"B: Back to the game menu","B: العودة لقائمة اللعبة","B : Menu du jeu","B: Menú del juego","B: Zurück zum Spielmenü","B: Menu do jogo","B: Menu di gioco","B: Меню игры","B: Oyun menüsü"}},
    {{"A: Play again","A: العب مجددا","A : Rejouer","A: Volver a jugar","A: Erneut spielen","A: Jogar novamente","A: Gioca ancora","A: Играть снова","A: Yeniden oyna"}},
    {{"A: Next player","A: اللاعب التالي","A : Joueur suivant","A: Siguiente jugador","A: Nächster Spieler","A: Próximo jogador","A: Prossimo giocatore","A: Следующий игрок","A: Sıradaki oyuncu"}},
    {{"Free move","حركة حرة","Mouvement libre","Movimiento libre","Freie Bewegung","Movimento livre","Movimento libero","Свободно","Serbest hareket"}},
    {{"No move","دون حركة","Sans mouvement","Sin movimiento","Keine Bewegung","Sem movimento","Senza movimento","Без движения","Hareket yok"}},
    {{"No move/pan/zoom","دون حركة أو دوران أو تقريب","Sans bouger, tourner, zoomer","Sin mover, girar ni acercar","Ohne Bewegung, Schwenk, Zoom","Sem mover, girar ou ampliar","Senza muovere, girare o zoomare","Без движения и масштаба","Hareket, dönüş, yakınlaştırma yok"}},
    {{"Up/Down: Select   A: Go   B: Back","أعلى/أسفل: تحديد A: اذهب B: رجوع","Haut/Bas : choisir A : aller B : retour","Arriba/Abajo: elegir A: ir B: volver","Oben/Unten: wählen A: los B: zurück","Cima/Baixo: escolher A: ir B: voltar","Su/Giù: scegli A: vai B: indietro","Вверх/Вниз: выбор A: идти B: назад","Yukarı/Aşağı: seç A: git B: geri"}},
    {{"Y: Add center   X: Delete","Y: إضافة المركز X: حذف","Y : ajouter le centre X : supprimer","Y: añadir centro X: borrar","Y: Mitte hinzufügen X: löschen","Y: adicionar centro X: excluir","Y: aggiungi centro X: elimina","Y: добавить центр X: удалить","Y: merkezi ekle X: sil"}},
    {{"Mystery expedition","رحلة غامضة","Expédition mystère","Expedición misteriosa","Geheimnisexpedition","Expedição misteriosa","Spedizione misteriosa","Тайная экспедиция","Gizemli keşif"}},
    {{"United States","الولايات المتحدة","États-Unis","Estados Unidos","Vereinigte Staaten","Estados Unidos","Stati Uniti","США","Amerika Birleşik Devletleri"}},
    {{"Explorer (hints)","مستكشف مع تلميحات","Explorateur (indices)","Explorador (pistas)","Entdecker (Hinweise)","Explorador (dicas)","Esploratore (indizi)","Исследователь (подсказки)","Kaşif (ipuçları)"}},
    {{"Expert NMPZ","خبير دون حركة أو تقريب","Expert NMPZ","Experto NMPZ","Experte NMPZ","Especialista NMPZ","Esperto NMPZ","Эксперт NMPZ","Uzman NMPZ"}},
};
const char *intl_language_name(unsigned lang){return names[lang<INTL_LANGUAGE_COUNT?lang:0];}
unsigned intl_language(void){return language;}
void intl_set_language(unsigned lang){language=lang<INTL_LANGUAGE_COUNT?lang:0;}
bool intl_music(void){return music;}
void intl_set_music(bool enabled){music=enabled;}
const char *intl_translate(const char *english){
    if(!english || !language)return english;
    const char *key=english;
    if(!strcmp(key,"Classic random"))key="Classic";
    if(!strcmp(key,"Moving"))key="Free move";
    for(size_t i=0;i<sizeof(messages)/sizeof(messages[0]);i++)
        if(!strcasecmp(key,messages[i].text[0]))return messages[i].text[language];
    return english;
}
bool intl_visual(const char *logical,char *visual,size_t capacity){
    if(!visual || !capacity)return false;
    visual[0]=0;if(!logical)return false;
    /* Bound stack usage and reject partial/oversized input rather than
     * silently corrupting UTF-8. Shape each paragraph independently. */
    size_t bytes=strlen(logical);
    if(bytes>=1024)return false;
    static FriBidiChar chars[1024];
    FriBidiStrIndex count=fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8,logical,(int)bytes,chars);
    if(count<0 || count>1023)return false;
    FriBidiStrIndex begin=0;
    for(FriBidiStrIndex end=0;end<=count;end++){
        if(end<count && chars[end]!='\n')continue;
        FriBidiStrIndex n=end-begin;
        if(n){
            static FriBidiCharType types[1024];static FriBidiBracketType brackets[1024];
            static FriBidiLevel levels[1024];static FriBidiArabicProp props[1024];
            FriBidiParType direction=FRIBIDI_PAR_ON;
            fribidi_get_bidi_types(chars+begin,n,types);
            fribidi_get_bracket_types(chars+begin,n,types,brackets);
            if(!fribidi_get_par_embedding_levels_ex(types,brackets,n,&direction,levels))return false;
            fribidi_get_joining_types(chars+begin,n,props);
            fribidi_join_arabic(types,n,levels,props);
            fribidi_shape(FRIBIDI_FLAGS_DEFAULT|FRIBIDI_FLAGS_ARABIC,levels,n,props,chars+begin);
            if(!fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT,types,n,0,direction,levels,chars+begin,NULL))return false;
        }
        begin=end+1;
    }
    count=fribidi_remove_bidi_marks(chars,count,NULL,NULL,NULL);
    /* UTF-8 needs at most four bytes per codepoint and one terminator. */
    static char result[4097];
    int written=fribidi_unicode_to_charset(FRIBIDI_CHAR_SET_UTF8,chars,count,result);
    if(written<0 || (size_t)written>=capacity)return false;
    memcpy(visual,result,(size_t)written);visual[written]=0;return true;
}
bool intl_load(const char *path){
    FILE *f=path?fopen(path,"rb"):NULL;if(!f)return false;
    unsigned version,lang,enabled;
    int n=fscanf(f,"%u %u %u",&version,&lang,&enabled);fclose(f);
    if(n!=3 || version!=1 || lang>=INTL_LANGUAGE_COUNT || enabled>1)return false;
    language=lang;music=enabled!=0;return true;
}
bool intl_save(const char *path){
    if(!path)return false;
    char tmp[320];int n=snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    if(n<0 || n>=(int)sizeof(tmp))return false;
    FILE *f=fopen(tmp,"wb");if(!f)return false;
    bool ok=fprintf(f,"1 %u %u\n",language,music?1u:0u)>0;
    if(fclose(f))ok=false;
    if(!ok || rename(tmp,path)){remove(tmp);return false;}return true;
}

int intl_snprintf(char *out,size_t capacity,const char *format,...){
    va_list args;va_start(args,format);
    int result=vsnprintf(out,capacity,intl_translate(format),args);
    va_end(args);
    if(out && capacity && result>=(int)capacity){
        size_t end=capacity-1,start=end;
        while(start && ((unsigned char)out[start-1]&0xc0)==0x80)start--;
        if(start)start--;
        unsigned char lead=(unsigned char)out[start];
        size_t bytes=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;
        if(start+bytes>end)out[start]=0;
    }
    return result;
}
