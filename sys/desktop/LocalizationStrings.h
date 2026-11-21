// Ekron Realms desktop HUD string table (X-macro).
//
// Every user-visible desktop string lives here, once, with all supported
// translations side by side so a missing translation is a compile error:
//
//   EKRON_STR(Key, "English", "Русский", "Português (Portugal)")
//
// Rules for translators:
//   * Keep "{0}", "{1}", ... placeholders intact (they are filled by
//     ekron::i18n::format) and keep key hints such as "[Q]" or "[Esc]".
//   * Only use characters covered by the built-in pixel font (DesktopFont.h):
//     ASCII, Russian Cyrillic, Portuguese accented letters and — × ° • « » ….
//     tests/native/localization_test.cpp enforces this.
//   * Portuguese strings follow European Portuguese (pt-PT) conventions
//     ("ecrã", "rato", "betão", "câmara", "ligar", "premir"...).
//
// This header is intentionally include-guard free: it is expanded several
// times by Localization.h / Localization.cpp with different EKRON_STR macros.

// ---------------------------------------------------------------------------
// Branding / window
// ---------------------------------------------------------------------------
EKRON_STR(WindowTitle,
          "EKRON — Urban Sandbox (Ekron Realms)",
          "ЭКРОН — Городская песочница (Ekron Realms)",
          "EKRON — Sandbox Urbano (Ekron Realms)")
EKRON_STR(BrandName, "EKRON", "ЭКРОН", "EKRON")

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------
EKRON_STR(ToastWelcome,
          "Welcome to EKRON! [Q] Catalog  [G] Store  [1-3] Tools  [Esc] Menu",
          "Добро пожаловать в ЭКРОН! [Q] Каталог  [G] Магазин  [1-3] Инструменты  [Esc] Меню",
          "Bem-vindo ao EKRON! [Q] Catálogo  [G] Loja  [1-3] Ferramentas  [Esc] Menu")
EKRON_STR(ToastStartFlightActivated,
          "Upgrade «Start with Flight» activated",
          "Улучшение «Старт с полётом» активировано",
          "Melhoria «Começar a Voar» ativada")
EKRON_STR(ToastPlayerRespawned,
          "Player returned to the spawn point",
          "Игрок возвращён на точку спавна",
          "Jogador devolvido ao ponto de spawn")
EKRON_STR(ToastTeleportRoad,
          "Moved to the nearest road",
          "Перемещение на ближайшую дорогу",
          "Transportado para a estrada mais próxima")
EKRON_STR(ToastCarRestored,
          "UAZ-469 restored to its parking spot",
          "УАЗ-469 восстановлен на стоянке",
          "UAZ-469 reposto no estacionamento")
EKRON_STR(ToastCarParkingBlocked,
          "UAZ-469 parking spot is occupied",
          "Место стоянки УАЗ-469 занято",
          "O lugar de estacionamento do UAZ-469 está ocupado")
EKRON_STR(ToastSpawnCopied,
          "spawn.json coordinates copied to the clipboard",
          "Координаты spawn.json скопированы в буфер обмена",
          "Coordenadas do spawn.json copiadas para a área de transferência")
EKRON_STR(ToastSpawnCopyFailed,
          "Could not copy the coordinates to the clipboard",
          "Не удалось скопировать координаты в буфер обмена",
          "Não foi possível copiar as coordenadas para a área de transferência")
EKRON_STR(ToastDroneInHands,
          "FPV drone in hands: [LMB / E] place it on the ground",
          "FPV-дрон в руках: [ЛКМ / E] поставить на землю",
          "Drone FPV nas mãos: [Clique Esq. / E] pousar no chão")
EKRON_STR(ToastItemSelected,
          "Selected: {0} ([LMB] place, [R] rotate, [RMB] cancel)",
          "Выбрано: {0} ([ЛКМ] поставить, [R] повернуть, [ПКМ] отмена)",
          "Selecionado: {0} ([Clique Esq.] colocar, [R] rodar, [Clique Dir.] cancelar)")
EKRON_STR(ToastDroneLeft,
          "Stopped piloting the FPV drone",
          "Выход из пилотирования FPV-дрона",
          "Saiu da pilotagem do drone FPV")
EKRON_STR(ToastDronePlaced,
          "FPV drone placed. Press [2] for the FPV remote",
          "FPV-дрон установлен. Нажмите [2] для пульта FPV",
          "Drone FPV colocado. Prima [2] para o comando FPV")
EKRON_STR(ToastDroneConnected,
          "Connected to the FPV drone! [W/S] throttle, [A/D] yaw, [Mouse] roll/pitch, [V] view",
          "Подключено к FPV-дрону! [W/S] тяга, [A/D] курс, [Мышь] крен/тангаж, [V] вид",
          "Ligado ao drone FPV! [W/S] aceleração, [A/D] guinada, [Rato] rolamento/arfagem, [V] vista")
EKRON_STR(ToastDronePilotingActive,
          "FPV drone piloting active",
          "Пилотирование FPV-дрона активно",
          "Pilotagem do drone FPV ativa")
EKRON_STR(ToastExitBlocked,
          "Exit blocked by an obstacle",
          "Выход заблокирован препятствием",
          "Saída bloqueada por um obstáculo")
EKRON_STR(ToastTool,
          "Tool: {0}",
          "Инструмент: {0}",
          "Ferramenta: {0}")
EKRON_STR(ToastDroneReturning,
          "FPV drone returning home",
          "FPV-дрон возвращается домой",
          "Drone FPV a regressar a casa")
EKRON_STR(ToastBuildCancelled,
          "Building cancelled",
          "Строительство отменено",
          "Construção cancelada")
EKRON_STR(ToastObjectRemoved,
          "Object removed",
          "Объект удалён",
          "Objeto removido")
EKRON_STR(ToastSkinApplied,
          "Skin applied: {0}",
          "Скин применён: {0}",
          "Skin aplicada: {0}")
EKRON_STR(ToastPurchaseRequesting,
          "Requesting purchase from the server…",
          "Запрос покупки у сервера…",
          "A pedir a compra ao servidor…")
EKRON_STR(ToastPurchased,
          "Purchased: {0}",
          "Куплено: {0}",
          "Comprado: {0}")
EKRON_STR(ToastPurchasedFromServer,
          "Purchase confirmed by the server",
          "Куплено у сервера",
          "Compra confirmada pelo servidor")
EKRON_STR(ToastPurchaseRejected,
          "Purchase rejected by the server",
          "Покупка отклонена сервером",
          "Compra recusada pelo servidor")
EKRON_STR(ToastLanguageChanged,
          "Language: {0}",
          "Язык: {0}",
          "Idioma: {0}")

// ---------------------------------------------------------------------------
// Hand tools / movement state
// ---------------------------------------------------------------------------
EKRON_STR(ToolGravityGun, "Gravity Gun", "Грави-пушка", "Arma de Gravidade")
EKRON_STR(ToolFpvRemote, "FPV Remote", "Пульт FPV", "Comando FPV")
EKRON_STR(ToolEmptyHands, "Empty Hands", "Пустые руки", "Mãos Vazias")
EKRON_STR(ToolBuilding, "Building", "Строительство", "Construção")
EKRON_STR(FlightOn,
          "Flight mode (Noclip): ON",
          "Режим полёта (Noclip): ВКЛ",
          "Modo de voo (Noclip): LIGADO")
EKRON_STR(FlightOff,
          "Flight mode (Noclip): OFF",
          "Режим полёта (Noclip): ВЫКЛ",
          "Modo de voo (Noclip): DESLIGADO")
EKRON_STR(StatusProne, "[PRONE]", "[ЛЁЖА]", "[DEITADO]")
EKRON_STR(StatusCrouched, "[CROUCHED]", "[ПРИСЕД]", "[AGACHADO]")

// ---------------------------------------------------------------------------
// Bottom controls bar
// ---------------------------------------------------------------------------
EKRON_STR(ControlsDrone,
          "[W/S] Throttle  [A/D] Yaw  [Mouse / Arrows] Roll & pitch  [V] Switch camera  [H] Home  [E / RMB] Exit",
          "[W/S] Тяга  [A/D] Курс  [Мышь / Стрелки] Крен и тангаж  [V] Смена камеры  [H] Домой  [E / ПКМ] Выйти",
          "[W/S] Aceleração  [A/D] Guinada  [Rato / Setas] Rolamento e arfagem  [V] Mudar câmara  [H] Casa  [E / Clique Dir.] Sair")
EKRON_STR(ControlsWalk,
          "[WASD] Walk  [Space] Jump  [1-3] Tool  [Q] Catalog  [LMB] Action  [R] Rotate  [E] Interact  [Esc] Menu",
          "[WASD] Ходьба  [Пробел] Прыжок  [1-3] Инструмент  [Q] Каталог  [ЛКМ] Действие  [R] Поворот  [E] Взаимодействие  [Esc] Меню",
          "[WASD] Andar  [Espaço] Saltar  [1-3] Ferramenta  [Q] Catálogo  [Clique Esq.] Ação  [R] Rodar  [E] Interagir  [Esc] Menu")

// ---------------------------------------------------------------------------
// Contextual interaction prompts
// ---------------------------------------------------------------------------
EKRON_STR(PromptExit, "[E] Exit", "[E] Выйти", "[E] Sair")
EKRON_STR(PromptPlaceDrone,
          "[LMB / E] Place the FPV drone on the ground",
          "[ЛКМ / E] Поставить FPV-дрон на землю",
          "[Clique Esq. / E] Pousar o drone FPV no chão")
EKRON_STR(PromptConnectDrone,
          "[LMB / E] Connect to the FPV drone",
          "[ЛКМ / E] Подключиться к FPV-дрону",
          "[Clique Esq. / E] Ligar ao drone FPV")
EKRON_STR(PromptEnterCar, "[E] Get in the UAZ-469", "[E] Сесть в УАЗ-469", "[E] Entrar no UAZ-469")
EKRON_STR(PromptSitSofa, "[E] Sit on the sofa", "[E] Присесть на диван", "[E] Sentar no sofá")
EKRON_STR(PromptCloseDoor, "[E] Close door", "[E] Закрыть дверь", "[E] Fechar a porta")
EKRON_STR(PromptOpenDoor, "[E] Open door", "[E] Открыть дверь", "[E] Abrir a porta")

// ---------------------------------------------------------------------------
// Build & equipment catalog
// ---------------------------------------------------------------------------
EKRON_STR(CatalogTitle,
          "BUILDING & EQUIPMENT CATALOG",
          "КАТАЛОГ СТРОИТЕЛЬСТВА И ТЕХНИКИ",
          "CATÁLOGO DE CONSTRUÇÃO E EQUIPAMENTO")
EKRON_STR(CatalogMaterials, "Materials", "Материалы", "Materiais")
EKRON_STR(CatalogConstruction, "Construction", "Строительство", "Construção")
EKRON_STR(CatalogFurniture, "Furniture", "Мебель", "Mobília")
EKRON_STR(CatalogEquipment, "Equipment", "Техника", "Equipamento")
EKRON_STR(ItemBoards, "Boards", "Доски", "Tábuas")
EKRON_STR(ItemBoardsSub, "Lumber", "Древесина", "Madeira")
EKRON_STR(ItemBrick, "Brick", "Кирпич", "Tijolo")
EKRON_STR(ItemBrickSub, "Brick pallet", "Поддон кирпича", "Palete de tijolos")
EKRON_STR(ItemConcrete, "Concrete", "Бетон", "Betão")
EKRON_STR(ItemConcreteSub, "Concrete block", "Бетонный блок", "Bloco de betão")
EKRON_STR(ItemFoamBlock, "Foam block", "Пеноблок", "Bloco leve")
EKRON_STR(ItemFoamBlockSub, "Wall block", "Стеновой блок", "Bloco de parede")
EKRON_STR(ItemWall, "Wall", "Стена", "Parede")
EKRON_STR(ItemWallSub, "Partition", "Перегородка", "Divisória")
EKRON_STR(ItemFloor, "Floor", "Пол", "Piso")
EKRON_STR(ItemFloorSub, "Floor slab", "Перекрытие", "Laje")
EKRON_STR(ItemWindow, "Window", "Окно", "Janela")
EKRON_STR(ItemWindowSub, "Window frame", "Оконная рама", "Caixilho")
EKRON_STR(ItemDoor, "Door", "Дверь", "Porta")
EKRON_STR(ItemDoorSub, "Interior", "Межкомнатная", "Interior")
EKRON_STR(ItemTable, "Table", "Стол", "Mesa")
EKRON_STR(ItemTableSub, "Wooden", "Деревянный", "De madeira")
EKRON_STR(ItemChair, "Chair", "Стул", "Cadeira")
EKRON_STR(ItemChairSub, "Stool / chair", "Табурет / стул", "Banco / cadeira")
EKRON_STR(ItemCabinet, "Cabinet", "Шкаф", "Armário")
EKRON_STR(ItemCabinetSub, "Shelving", "Стеллаж", "Estante")
EKRON_STR(ItemSofa, "Sofa", "Диван", "Sofá")
EKRON_STR(ItemSofaSub, "Soft sofa", "Мягкий диван", "Sofá estofado")
EKRON_STR(ItemUaz, "UAZ-469", "УАЗ-469", "UAZ-469")
EKRON_STR(ItemUazSub, "Off-roader", "Внедорожник", "Todo-o-terreno")
EKRON_STR(ItemFpvDrone, "FPV drone", "FPV-дрон", "Drone FPV")
EKRON_STR(ItemFpvDroneSub, "Take in hands", "Взять в руки", "Pegar nas mãos")
EKRON_STR(ItemObject, "Object", "Объект", "Objeto")

// ---------------------------------------------------------------------------
// Store (shop)
// ---------------------------------------------------------------------------
EKRON_STR(ShopTitle, "STORE — EKRON", "МАГАЗИН — ЭКРОН", "LOJA — EKRON")
EKRON_STR(ShopGold, "GOLD: {0}", "ЗОЛОТО: {0}", "OURO: {0}")
EKRON_STR(ShopButtonEquipped, "EQUIPPED", "ПРИМЕНЕНО", "EQUIPADO")
EKRON_STR(ShopButtonEquip, "EQUIP", "НАДЕТЬ", "EQUIPAR")
EKRON_STR(ShopButtonActive, "ACTIVE", "АКТИВНО", "ATIVO")
EKRON_STR(ShopButtonBuy, "BUY {0}", "КУПИТЬ {0}", "COMPRAR {0}")
EKRON_STR(ShopCategorySkins, "WEAPON SKINS", "СКИНЫ ВОЙНУХИ", "SKINS DE ARMAS")
EKRON_STR(ShopCategoryExtras, "UPGRADES", "УЛУЧШЕНИЯ", "MELHORIAS")
EKRON_STR(ShopErrorUnknownItem, "Unknown item", "Неизвестный товар", "Artigo desconhecido")
EKRON_STR(ShopErrorAlreadyOwned, "Already purchased", "Уже куплено", "Já comprado")
EKRON_STR(ShopErrorNotEnoughGold, "Not enough gold", "Недостаточно золота", "Ouro insuficiente")
EKRON_STR(ShopSkinFlame, "Flame", "Пламя", "Chama")
EKRON_STR(ShopSkinFlameDesc,
          "Bright red gravity-gun blast",
          "Ярко-красный выброс грави-пушки",
          "Disparo vermelho vivo da arma")
EKRON_STR(ShopSkinFrost, "Frost", "Ледяной", "Gelo")
EKRON_STR(ShopSkinFrostDesc, "Cold blue plasma", "Холодная голубая плазма", "Plasma azul gelado")
EKRON_STR(ShopSkinEmerald, "Emerald", "Изумруд", "Esmeralda")
EKRON_STR(ShopSkinEmeraldDesc, "Bright green energy", "Ярко-зелёная энергия", "Energia verde viva")
EKRON_STR(ShopSkinAzure, "Azure", "Азур", "Celeste")
EKRON_STR(ShopSkinAzureDesc, "Calm blue plasma", "Спокойная синяя плазма", "Plasma azul sereno")
EKRON_STR(ShopSkinGoldenSeal, "Golden Seal", "Золотая печать", "Selo Dourado")
EKRON_STR(ShopSkinGoldenSealDesc, "Sparkling golden glow", "Искристое золотое свечение", "Brilho dourado cintilante")
EKRON_STR(ShopSkinVioletStorm, "Violet Storm", "Фиолетовая буря", "Tempestade Violeta")
EKRON_STR(ShopSkinVioletStormDesc, "Deep violet pulse", "Глубокий фиолетовый импульс", "Pulso violeta profundo")
EKRON_STR(ShopSkinToxicRose, "Toxic Rose", "Ядовитая роза", "Rosa Tóxica")
EKRON_STR(ShopSkinToxicRoseDesc, "Bright pink toxic glow", "Ярко-розовый ядовитый свет", "Luz tóxica cor-de-rosa")
EKRON_STR(ShopSkinNeon, "Neon", "Неон", "Néon")
EKRON_STR(ShopSkinNeonDesc, "Sharp neon white-blue", "Острая неоновая бело-голубая", "Néon branco-azulado intenso")
EKRON_STR(ShopExtraBonusWallet, "Bonus Wallet", "Бонусный кошелёк", "Carteira Bónus")
EKRON_STR(ShopExtraBonusWalletDesc, "Instant +500 gold coins", "Мгновенно +500 золотых монет", "+500 moedas de ouro já")
EKRON_STR(ShopExtraDoubleCoins, "Double Coins", "Двойные монеты", "Moedas a Dobrar")
EKRON_STR(ShopExtraDoubleCoinsDesc, "Coin earning rate ×2", "Скорость накопления монет ×2", "Ganho de moedas ×2")
EKRON_STR(ShopExtraGoldenCrosshair, "Golden Crosshair", "Золотой прицел", "Mira Dourada")
EKRON_STR(ShopExtraGoldenCrosshairDesc, "Golden crosshair on screen", "Золотой прицел на экране", "Mira dourada no ecrã")
EKRON_STR(ShopExtraCamoHud, "Camo HUD", "Камуфляж HUD", "HUD Camuflado")
EKRON_STR(ShopExtraCamoHudDesc, "Camouflage interface colours", "Камуфляжная расцветка интерфейса", "Interface com camuflagem")
EKRON_STR(ShopExtraNeonSight, "Neon Sight", "Неоновый прицел", "Mira Néon")
EKRON_STR(ShopExtraNeonSightDesc, "Glowing neon crosshair", "Неоновый прицел со свечением", "Mira néon luminosa")
EKRON_STR(ShopExtraStartWithFlight, "Start with Flight", "Старт с полётом", "Começar a Voar")
EKRON_STR(ShopExtraStartWithFlightDesc, "Respawn with flight enabled", "Возвращение на спавн с полётом", "Reaparecer já a voar")
EKRON_STR(ShopEffectCoinBonus, "+500 coins", "+500 монет", "+500 moedas")
EKRON_STR(ShopEffectDoubleCoins, "×2 coin rate", "×2 скорость монет", "×2 ritmo de moedas")

// ---------------------------------------------------------------------------
// Pause menu
// ---------------------------------------------------------------------------
EKRON_STR(PauseTitle, "PAUSED — EKRON", "ПАУЗА — ЭКРОН", "PAUSA — EKRON")
EKRON_STR(PauseResume, "Resume game", "Продолжить игру", "Continuar o jogo")
EKRON_STR(PauseGoToRoad, "Go to the road", "Переместиться на дорогу", "Ir para a estrada")
EKRON_STR(PauseRestoreCar, "Restore UAZ-469", "Восстановить УАЗ-469", "Repor o UAZ-469")
EKRON_STR(PauseRespawn, "Return to spawn point", "Вернуться на точку спавна", "Voltar ao ponto de spawn")
EKRON_STR(PauseCopySpawn, "Copy spawn.json", "Скопировать spawn.json", "Copiar spawn.json")
EKRON_STR(PauseLanguage, "Language: {0}", "Язык: {0}", "Idioma: {0}")
EKRON_STR(PauseQuit, "Quit to desktop", "Выйти на рабочий стол", "Sair para o ambiente de trabalho")

// ---------------------------------------------------------------------------
// Online presence status (mirrors DesktopOnlineClient status strings)
// ---------------------------------------------------------------------------
EKRON_STR(OnlineOffline, "Offline", "Не в сети", "Desligado")
EKRON_STR(OnlineInvalidSettings, "Invalid online settings", "Неверные настройки онлайна", "Definições online inválidas")
EKRON_STR(OnlineConnecting, "Connecting…", "Подключение…", "A ligar…")
EKRON_STR(OnlinePlayersOnly, "Online: players only", "Онлайн: только игроки", "Online: apenas jogadores")
EKRON_STR(OnlineConnectionClosed, "Connection closed", "Соединение закрыто", "Ligação terminada")
EKRON_STR(OnlineConnectionFailed,
          "Connection failed (TLS or credentials)",
          "Ошибка подключения (TLS или ключ)",
          "Falha na ligação (TLS ou credenciais)")
EKRON_STR(OnlineOpenSslMissing,
          "Online unavailable: OpenSSL missing",
          "Онлайн недоступен: нет OpenSSL",
          "Online indisponível: falta o OpenSSL")
