// Ekron Realms desktop HUD string table (X-macro).
//
// Every user-visible desktop string lives here, once, with all supported
// translations side by side so a missing translation is a compile error:
//
//   EKRON_STR(Key, "English", "Русский", "Português (Portugal)", "Español (España)")
//
// Rules for translators:
//   * Keep "{0}", "{1}", ... placeholders intact (they are filled by
//     ekron::i18n::format) and keep key hints such as "[Q]" or "[Esc]".
//   * Only use characters covered by the built-in pixel font (DesktopFont.h):
//     ASCII, Russian Cyrillic, Portuguese and Spanish accented letters
//     (incl. ñ ü ¡ ¿) and — × ° • « » ….
//     tests/native/localization_test.cpp enforces this.
//   * Portuguese strings follow European Portuguese (pt-PT) conventions
//     ("ecrã", "rato", "betão", "câmara", "ligar", "premir"...).
//   * Spanish strings follow Spain (es-ES) usage ("ratón", "hormigón",
//     "mando", "aparcamiento", "coche") and address the player with "tú"
//     ("Pulsa", "Elige"), as Spanish game UIs do.
//
// This header is intentionally include-guard free: it is expanded several
// times by Localization.h / Localization.cpp with different EKRON_STR macros.

// ---------------------------------------------------------------------------
// Branding / window
// ---------------------------------------------------------------------------
EKRON_STR(WindowTitle,
          "EKRON — Urban Sandbox (Ekron Realms)",
          "ЭКРОН — Городская песочница (Ekron Realms)",
          "EKRON — Sandbox Urbano (Ekron Realms)",
          "EKRON — Sandbox urbano (Ekron Realms)")
EKRON_STR(BrandName, "EKRON", "ЭКРОН", "EKRON", "EKRON")
EKRON_STR(IntroSkip,
          "[Esc] Skip intro",
          "[Esc] Пропустить заставку",
          "[Esc] Saltar introdução",
          "[Esc] Saltar intro")
EKRON_STR(LoadingWorld,
          "Loading the world…",
          "Загрузка мира…",
          "A carregar o mundo…",
          "Cargando el mundo…")

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------
EKRON_STR(ToastWelcome,
          "Welcome to EKRON! [Q] Catalog  [G] Store  [1-3] Tools  [Esc] Menu",
          "Добро пожаловать в ЭКРОН! [Q] Каталог  [G] Магазин  [1-3] Инструменты  [Esc] Меню",
          "Bem-vindo ao EKRON! [Q] Catálogo  [G] Loja  [1-3] Ferramentas  [Esc] Menu",
          "¡Bienvenido a EKRON! [Q] Catálogo  [G] Tienda  [1-3] Herramientas  [Esc] Menú")
EKRON_STR(ToastStartFlightActivated,
          "Upgrade «Start with Flight» activated",
          "Улучшение «Старт с полётом» активировано",
          "Melhoria «Começar a Voar» ativada",
          "Mejora «Empezar volando» activada")
EKRON_STR(ToastPlayerRespawned,
          "Player returned to the spawn point",
          "Игрок возвращён на точку спавна",
          "Jogador devolvido ao ponto de spawn",
          "Jugador devuelto al punto de aparición")
EKRON_STR(ToastTeleportRoad,
          "Moved to the nearest road",
          "Перемещение на ближайшую дорогу",
          "Transportado para a estrada mais próxima",
          "Trasladado a la carretera más cercana")
EKRON_STR(ToastCarRestored,
          "UAZ-469 restored to its parking spot",
          "УАЗ-469 восстановлен на стоянке",
          "UAZ-469 reposto no estacionamento",
          "UAZ-469 devuelto a su plaza de aparcamiento")
EKRON_STR(ToastCarParkingBlocked,
          "UAZ-469 parking spot is occupied",
          "Место стоянки УАЗ-469 занято",
          "O lugar de estacionamento do UAZ-469 está ocupado",
          "La plaza de aparcamiento del UAZ-469 está ocupada")
EKRON_STR(ToastSpawnCopied,
          "spawn.json coordinates copied to the clipboard",
          "Координаты spawn.json скопированы в буфер обмена",
          "Coordenadas do spawn.json copiadas para a área de transferência",
          "Coordenadas de spawn.json copiadas al portapapeles")
EKRON_STR(ToastSpawnCopyFailed,
          "Could not copy the coordinates to the clipboard",
          "Не удалось скопировать координаты в буфер обмена",
          "Não foi possível copiar as coordenadas para a área de transferência",
          "No se han podido copiar las coordenadas al portapapeles")
EKRON_STR(ToastDroneInHands,
          "FPV drone in hands: [LMB / E] place it on the ground",
          "FPV-дрон в руках: [ЛКМ / E] поставить на землю",
          "Drone FPV nas mãos: [Clique Esq. / E] pousar no chão",
          "Dron FPV en las manos: [Clic izq. / E] posarlo en el suelo")
EKRON_STR(ToastItemSelected,
          "Selected: {0} ([LMB] place, [R] rotate, [RMB] cancel)",
          "Выбрано: {0} ([ЛКМ] поставить, [R] повернуть, [ПКМ] отмена)",
          "Selecionado: {0} ([Clique Esq.] colocar, [R] rodar, [Clique Dir.] cancelar)",
          "Seleccionado: {0} ([Clic izq.] colocar, [R] girar, [Clic der.] cancelar)")
EKRON_STR(ToastDroneLeft,
          "Stopped piloting the FPV drone",
          "Выход из пилотирования FPV-дрона",
          "Saiu da pilotagem do drone FPV",
          "Has dejado de pilotar el dron FPV")
EKRON_STR(ToastDronePlaced,
          "FPV drone placed. Press [2] for the FPV remote",
          "FPV-дрон установлен. Нажмите [2] для пульта FPV",
          "Drone FPV colocado. Prima [2] para o comando FPV",
          "Dron FPV colocado. Pulsa [2] para usar el mando FPV")
EKRON_STR(ToastDroneConnected,
          "Connected to the FPV drone! [W/S] throttle, [A/D] yaw, [Mouse] roll/pitch, [V] view",
          "Подключено к FPV-дрону! [W/S] тяга, [A/D] курс, [Мышь] крен/тангаж, [V] вид",
          "Ligado ao drone FPV! [W/S] aceleração, [A/D] guinada, [Rato] rolamento/arfagem, [V] vista",
          "¡Conectado al dron FPV! [W/S] acelerador, [A/D] guiñada, [Ratón] alabeo/cabeceo, [V] vista")
EKRON_STR(ToastDronePilotingActive,
          "FPV drone piloting active",
          "Пилотирование FPV-дрона активно",
          "Pilotagem do drone FPV ativa",
          "Pilotaje del dron FPV activo")
EKRON_STR(ToastExitBlocked,
          "Exit blocked by an obstacle",
          "Выход заблокирован препятствием",
          "Saída bloqueada por um obstáculo",
          "Salida bloqueada por un obstáculo")
EKRON_STR(ToastTool,
          "Tool: {0}",
          "Инструмент: {0}",
          "Ferramenta: {0}",
          "Herramienta: {0}")
EKRON_STR(ToastDroneReturning,
          "FPV drone returning home",
          "FPV-дрон возвращается домой",
          "Drone FPV a regressar a casa",
          "El dron FPV vuelve a casa")
EKRON_STR(ToastBuildCancelled,
          "Building cancelled",
          "Строительство отменено",
          "Construção cancelada",
          "Construcción cancelada")
EKRON_STR(ToastObjectRemoved,
          "Object removed",
          "Объект удалён",
          "Objeto removido",
          "Objeto eliminado")
EKRON_STR(ToastSkinApplied,
          "Skin applied: {0}",
          "Скин применён: {0}",
          "Skin aplicada: {0}",
          "Aspecto aplicado: {0}")
EKRON_STR(ToastPurchaseRequesting,
          "Requesting purchase from the server…",
          "Запрос покупки у сервера…",
          "A pedir a compra ao servidor…",
          "Solicitando la compra al servidor…")
EKRON_STR(ToastPurchased,
          "Purchased: {0}",
          "Куплено: {0}",
          "Comprado: {0}",
          "Comprado: {0}")
EKRON_STR(ToastPurchasedFromServer,
          "Purchase confirmed by the server",
          "Куплено у сервера",
          "Compra confirmada pelo servidor",
          "Compra confirmada por el servidor")
EKRON_STR(ToastPurchaseRejected,
          "Purchase rejected by the server",
          "Покупка отклонена сервером",
          "Compra recusada pelo servidor",
          "Compra rechazada por el servidor")
EKRON_STR(ToastLanguageChanged,
          "Language: {0}",
          "Язык: {0}",
          "Idioma: {0}",
          "Idioma: {0}")

// ---------------------------------------------------------------------------
// Hand tools / movement state
// ---------------------------------------------------------------------------
EKRON_STR(ToolGravityGun, "Gravity Gun", "Грави-пушка", "Arma de Gravidade", "Pistola de gravedad")
EKRON_STR(ToolFpvRemote, "FPV Remote", "Пульт FPV", "Comando FPV", "Mando FPV")
EKRON_STR(ToolEmptyHands, "Empty Hands", "Пустые руки", "Mãos Vazias", "Manos vacías")
EKRON_STR(ToolBuilding, "Building", "Строительство", "Construção", "Construcción")
EKRON_STR(FlightOn,
          "Flight mode (Noclip): ON",
          "Режим полёта (Noclip): ВКЛ",
          "Modo de voo (Noclip): LIGADO",
          "Modo de vuelo (Noclip): ACTIVADO")
EKRON_STR(FlightOff,
          "Flight mode (Noclip): OFF",
          "Режим полёта (Noclip): ВЫКЛ",
          "Modo de voo (Noclip): DESLIGADO",
          "Modo de vuelo (Noclip): DESACTIVADO")
EKRON_STR(StatusProne, "[PRONE]", "[ЛЁЖА]", "[DEITADO]", "[TUMBADO]")
EKRON_STR(StatusCrouched, "[CROUCHED]", "[ПРИСЕД]", "[AGACHADO]", "[AGACHADO]")

// ---------------------------------------------------------------------------
// Bottom controls bar
// ---------------------------------------------------------------------------
EKRON_STR(ControlsDrone,
          "[W/S] Throttle  [A/D] Yaw  [Mouse / Arrows] Roll & pitch  [V] Switch camera  [H] Home  [E / RMB] Exit",
          "[W/S] Тяга  [A/D] Курс  [Мышь / Стрелки] Крен и тангаж  [V] Смена камеры  [H] Домой  [E / ПКМ] Выйти",
          "[W/S] Aceleração  [A/D] Guinada  [Rato / Setas] Rolamento e arfagem  [V] Mudar câmara  [H] Casa  [E / Clique Dir.] Sair",
          "[W/S] Acelerador  [A/D] Guiñada  [Ratón / Flechas] Alabeo y cabeceo  [V] Cambiar cámara  [H] Casa  [E / Clic der.] Salir")
EKRON_STR(ControlsWalk,
          "[WASD] Walk  [Space] Jump  [1-3] Tool  [Q] Catalog  [LMB] Action  [R] Rotate  [E] Interact  [Esc] Menu",
          "[WASD] Ходьба  [Пробел] Прыжок  [1-3] Инструмент  [Q] Каталог  [ЛКМ] Действие  [R] Поворот  [E] Взаимодействие  [Esc] Меню",
          "[WASD] Andar  [Espaço] Saltar  [1-3] Ferramenta  [Q] Catálogo  [Clique Esq.] Ação  [R] Rodar  [E] Interagir  [Esc] Menu",
          "[WASD] Andar  [Espacio] Saltar  [1-3] Herramienta  [Q] Catálogo  [Clic izq.] Acción  [R] Girar  [E] Interactuar  [Esc] Menú")

// ---------------------------------------------------------------------------
// Contextual interaction prompts
// ---------------------------------------------------------------------------
EKRON_STR(PromptExit, "[E] Exit", "[E] Выйти", "[E] Sair", "[E] Salir")
EKRON_STR(PromptPlaceDrone,
          "[LMB / E] Place the FPV drone on the ground",
          "[ЛКМ / E] Поставить FPV-дрон на землю",
          "[Clique Esq. / E] Pousar o drone FPV no chão",
          "[Clic izq. / E] Posar el dron FPV en el suelo")
EKRON_STR(PromptConnectDrone,
          "[LMB / E] Connect to the FPV drone",
          "[ЛКМ / E] Подключиться к FPV-дрону",
          "[Clique Esq. / E] Ligar ao drone FPV",
          "[Clic izq. / E] Conectar con el dron FPV")
EKRON_STR(PromptEnterCar, "[E] Get in the UAZ-469", "[E] Сесть в УАЗ-469", "[E] Entrar no UAZ-469", "[E] Subir al UAZ-469")
EKRON_STR(PromptSitSofa, "[E] Sit on the sofa", "[E] Присесть на диван", "[E] Sentar no sofá", "[E] Sentarse en el sofá")
EKRON_STR(PromptCloseDoor, "[E] Close door", "[E] Закрыть дверь", "[E] Fechar a porta", "[E] Cerrar la puerta")
EKRON_STR(PromptOpenDoor, "[E] Open door", "[E] Открыть дверь", "[E] Abrir a porta", "[E] Abrir la puerta")

// ---------------------------------------------------------------------------
// Build & equipment catalog
// ---------------------------------------------------------------------------
EKRON_STR(CatalogTitle,
          "BUILDING & EQUIPMENT CATALOG",
          "КАТАЛОГ СТРОИТЕЛЬСТВА И ТЕХНИКИ",
          "CATÁLOGO DE CONSTRUÇÃO E EQUIPAMENTO",
          "CATÁLOGO DE CONSTRUCCIÓN Y EQUIPAMIENTO")
EKRON_STR(CatalogMaterials, "Materials", "Материалы", "Materiais", "Materiales")
EKRON_STR(CatalogConstruction, "Construction", "Строительство", "Construção", "Construcción")
EKRON_STR(CatalogFurniture, "Furniture", "Мебель", "Mobília", "Muebles")
EKRON_STR(CatalogEquipment, "Equipment", "Техника", "Equipamento", "Equipamiento")
EKRON_STR(ItemBoards, "Boards", "Доски", "Tábuas", "Tablas")
EKRON_STR(ItemBoardsSub, "Lumber", "Древесина", "Madeira", "Madera")
EKRON_STR(ItemBrick, "Brick", "Кирпич", "Tijolo", "Ladrillo")
EKRON_STR(ItemBrickSub, "Brick pallet", "Поддон кирпича", "Palete de tijolos", "Palé de ladrillos")
EKRON_STR(ItemConcrete, "Concrete", "Бетон", "Betão", "Hormigón")
EKRON_STR(ItemConcreteSub, "Concrete block", "Бетонный блок", "Bloco de betão", "Bloque de hormigón")
EKRON_STR(ItemFoamBlock, "Foam block", "Пеноблок", "Bloco leve", "Bloque ligero")
EKRON_STR(ItemFoamBlockSub, "Wall block", "Стеновой блок", "Bloco de parede", "Bloque de pared")
EKRON_STR(ItemWall, "Wall", "Стена", "Parede", "Pared")
EKRON_STR(ItemWallSub, "Partition", "Перегородка", "Divisória", "Tabique")
EKRON_STR(ItemFloor, "Floor", "Пол", "Piso", "Suelo")
EKRON_STR(ItemFloorSub, "Floor slab", "Перекрытие", "Laje", "Losa")
EKRON_STR(ItemWindow, "Window", "Окно", "Janela", "Ventana")
EKRON_STR(ItemWindowSub, "Window frame", "Оконная рама", "Caixilho", "Marco de ventana")
EKRON_STR(ItemDoor, "Door", "Дверь", "Porta", "Puerta")
EKRON_STR(ItemDoorSub, "Interior", "Межкомнатная", "Interior", "Interior")
EKRON_STR(ItemTable, "Table", "Стол", "Mesa", "Mesa")
EKRON_STR(ItemTableSub, "Wooden", "Деревянный", "De madeira", "De madera")
EKRON_STR(ItemChair, "Chair", "Стул", "Cadeira", "Silla")
EKRON_STR(ItemChairSub, "Stool / chair", "Табурет / стул", "Banco / cadeira", "Taburete / silla")
EKRON_STR(ItemCabinet, "Cabinet", "Шкаф", "Armário", "Armario")
EKRON_STR(ItemCabinetSub, "Shelving", "Стеллаж", "Estante", "Estantería")
EKRON_STR(ItemSofa, "Sofa", "Диван", "Sofá", "Sofá")
EKRON_STR(ItemSofaSub, "Soft sofa", "Мягкий диван", "Sofá estofado", "Sofá mullido")
EKRON_STR(ItemUaz, "UAZ-469", "УАЗ-469", "UAZ-469", "UAZ-469")
EKRON_STR(ItemUazSub, "Off-roader", "Внедорожник", "Todo-o-terreno", "Todoterreno")
EKRON_STR(ItemFpvDrone, "FPV drone", "FPV-дрон", "Drone FPV", "Dron FPV")
EKRON_STR(ItemFpvDroneSub, "Take in hands", "Взять в руки", "Pegar nas mãos", "Coger en mano")
EKRON_STR(ItemObject, "Object", "Объект", "Objeto", "Objeto")

// ---------------------------------------------------------------------------
// Store (shop)
// ---------------------------------------------------------------------------
EKRON_STR(ShopTitle, "STORE — EKRON", "МАГАЗИН — ЭКРОН", "LOJA — EKRON", "TIENDA — EKRON")
EKRON_STR(ShopGold, "GOLD: {0}", "ЗОЛОТО: {0}", "OURO: {0}", "ORO: {0}")
EKRON_STR(ShopButtonEquipped, "EQUIPPED", "ПРИМЕНЕНО", "EQUIPADO", "EQUIPADO")
EKRON_STR(ShopButtonEquip, "EQUIP", "НАДЕТЬ", "EQUIPAR", "EQUIPAR")
EKRON_STR(ShopButtonActive, "ACTIVE", "АКТИВНО", "ATIVO", "ACTIVO")
EKRON_STR(ShopButtonBuy, "BUY {0}", "КУПИТЬ {0}", "COMPRAR {0}", "COMPRAR {0}")
EKRON_STR(ShopCategorySkins, "WEAPON SKINS", "СКИНЫ ВОЙНУХИ", "SKINS DE ARMAS", "ASPECTOS DE ARMAS")
EKRON_STR(ShopCategoryExtras, "UPGRADES", "УЛУЧШЕНИЯ", "MELHORIAS", "MEJORAS")
EKRON_STR(ShopErrorUnknownItem, "Unknown item", "Неизвестный товар", "Artigo desconhecido", "Artículo desconocido")
EKRON_STR(ShopErrorAlreadyOwned, "Already purchased", "Уже куплено", "Já comprado", "Ya comprado")
EKRON_STR(ShopErrorNotEnoughGold, "Not enough gold", "Недостаточно золота", "Ouro insuficiente", "Oro insuficiente")
EKRON_STR(ShopSkinFlame, "Flame", "Пламя", "Chama", "Llama")
EKRON_STR(ShopSkinFlameDesc,
          "Bright red gravity-gun blast",
          "Ярко-красный выброс грави-пушки",
          "Disparo vermelho vivo da arma",
          "Disparo rojo intenso de la pistola de gravedad")
EKRON_STR(ShopSkinFrost, "Frost", "Ледяной", "Gelo", "Escarcha")
EKRON_STR(ShopSkinFrostDesc, "Cold blue plasma", "Холодная голубая плазма", "Plasma azul gelado", "Plasma azul helado")
EKRON_STR(ShopSkinEmerald, "Emerald", "Изумруд", "Esmeralda", "Esmeralda")
EKRON_STR(ShopSkinEmeraldDesc, "Bright green energy", "Ярко-зелёная энергия", "Energia verde viva", "Energía verde intensa")
EKRON_STR(ShopSkinAzure, "Azure", "Азур", "Celeste", "Celeste")
EKRON_STR(ShopSkinAzureDesc, "Calm blue plasma", "Спокойная синяя плазма", "Plasma azul sereno", "Plasma azul sereno")
EKRON_STR(ShopSkinGoldenSeal, "Golden Seal", "Золотая печать", "Selo Dourado", "Sello dorado")
EKRON_STR(ShopSkinGoldenSealDesc, "Sparkling golden glow", "Искристое золотое свечение", "Brilho dourado cintilante", "Brillo dorado centelleante")
EKRON_STR(ShopSkinVioletStorm, "Violet Storm", "Фиолетовая буря", "Tempestade Violeta", "Tormenta violeta")
EKRON_STR(ShopSkinVioletStormDesc, "Deep violet pulse", "Глубокий фиолетовый импульс", "Pulso violeta profundo", "Pulso violeta profundo")
EKRON_STR(ShopSkinToxicRose, "Toxic Rose", "Ядовитая роза", "Rosa Tóxica", "Rosa tóxica")
EKRON_STR(ShopSkinToxicRoseDesc, "Bright pink toxic glow", "Ярко-розовый ядовитый свет", "Luz tóxica cor-de-rosa", "Brillo tóxico rosa intenso")
EKRON_STR(ShopSkinNeon, "Neon", "Неон", "Néon", "Neón")
EKRON_STR(ShopSkinNeonDesc, "Sharp neon white-blue", "Острая неоновая бело-голубая", "Néon branco-azulado intenso", "Neón blanco azulado intenso")
EKRON_STR(ShopExtraBonusWallet, "Bonus Wallet", "Бонусный кошелёк", "Carteira Bónus", "Cartera extra")
EKRON_STR(ShopExtraBonusWalletDesc, "Instant +500 gold coins", "Мгновенно +500 золотых монет", "+500 moedas de ouro já", "+500 monedas de oro al instante")
EKRON_STR(ShopExtraDoubleCoins, "Double Coins", "Двойные монеты", "Moedas a Dobrar", "Monedas dobles")
EKRON_STR(ShopExtraDoubleCoinsDesc, "Coin earning rate ×2", "Скорость накопления монет ×2", "Ganho de moedas ×2", "Ganancia de monedas ×2")
EKRON_STR(ShopExtraGoldenCrosshair, "Golden Crosshair", "Золотой прицел", "Mira Dourada", "Mira dorada")
EKRON_STR(ShopExtraGoldenCrosshairDesc, "Golden crosshair on screen", "Золотой прицел на экране", "Mira dourada no ecrã", "Mira dorada en pantalla")
EKRON_STR(ShopExtraCamoHud, "Camo HUD", "Камуфляж HUD", "HUD Camuflado", "HUD de camuflaje")
EKRON_STR(ShopExtraCamoHudDesc, "Camouflage interface colours", "Камуфляжная расцветка интерфейса", "Interface com camuflagem", "Colores de camuflaje en la interfaz")
EKRON_STR(ShopExtraNeonSight, "Neon Sight", "Неоновый прицел", "Mira Néon", "Mira de neón")
EKRON_STR(ShopExtraNeonSightDesc, "Glowing neon crosshair", "Неоновый прицел со свечением", "Mira néon luminosa", "Mira de neón luminosa")
EKRON_STR(ShopExtraStartWithFlight, "Start with Flight", "Старт с полётом", "Começar a Voar", "Empezar volando")
EKRON_STR(ShopExtraStartWithFlightDesc, "Respawn with flight enabled", "Возвращение на спавн с полётом", "Reaparecer já a voar", "Reaparece con el vuelo activado")
EKRON_STR(ShopEffectCoinBonus, "+500 coins", "+500 монет", "+500 moedas", "+500 monedas")
EKRON_STR(ShopEffectDoubleCoins, "×2 coin rate", "×2 скорость монет", "×2 ritmo de moedas", "×2 ritmo de monedas")

// ---------------------------------------------------------------------------
// Pause menu
// ---------------------------------------------------------------------------
EKRON_STR(PauseTitle, "PAUSED — EKRON", "ПАУЗА — ЭКРОН", "PAUSA — EKRON", "PAUSA — EKRON")
EKRON_STR(PauseResume, "Resume game", "Продолжить игру", "Continuar o jogo", "Reanudar partida")
EKRON_STR(PauseGoToRoad, "Go to the road", "Переместиться на дорогу", "Ir para a estrada", "Ir a la carretera")
EKRON_STR(PauseRestoreCar, "Restore UAZ-469", "Восстановить УАЗ-469", "Repor o UAZ-469", "Restablecer el UAZ-469")
EKRON_STR(PauseRespawn, "Return to spawn point", "Вернуться на точку спавна", "Voltar ao ponto de spawn", "Volver al punto de aparición")
EKRON_STR(PauseCopySpawn, "Copy spawn.json", "Скопировать spawn.json", "Copiar spawn.json", "Copiar spawn.json")
EKRON_STR(PauseLanguage, "Language: {0}", "Язык: {0}", "Idioma: {0}", "Idioma: {0}")
EKRON_STR(PauseQuit, "Quit to desktop", "Выйти на рабочий стол", "Sair para o ambiente de trabalho", "Salir al escritorio")

// ---------------------------------------------------------------------------
// Online presence status (mirrors DesktopOnlineClient status strings)
// ---------------------------------------------------------------------------
EKRON_STR(OnlineOffline, "Offline", "Не в сети", "Desligado", "Sin conexión")
EKRON_STR(OnlineInvalidSettings, "Invalid online settings", "Неверные настройки онлайна", "Definições online inválidas", "Ajustes en línea no válidos")
EKRON_STR(OnlineConnecting, "Connecting…", "Подключение…", "A ligar…", "Conectando…")
EKRON_STR(OnlinePlayersOnly, "Online: players only", "Онлайн: только игроки", "Online: apenas jogadores", "En línea: solo jugadores")
EKRON_STR(OnlineConnectionClosed, "Connection closed", "Соединение закрыто", "Ligação terminada", "Conexión cerrada")
EKRON_STR(OnlineConnectionFailed,
          "Connection failed (TLS or credentials)",
          "Ошибка подключения (TLS или ключ)",
          "Falha na ligação (TLS ou credenciais)",
          "Error de conexión (TLS o credenciales)")
EKRON_STR(OnlineOpenSslMissing,
          "Online unavailable: OpenSSL missing",
          "Онлайн недоступен: нет OpenSSL",
          "Online indisponível: falta o OpenSSL",
          "En línea no disponible: falta OpenSSL")
