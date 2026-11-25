//
//  GameViewController.swift
//  realms iOS
//

import UIKit
import MetalKit
import AVFoundation
import UniformTypeIdentifiers

class GameViewController: UIViewController {

    private var mtkView: MTKView!
    private var bridge: SandboxBridge?
    private let online = OnlinePresence()

    // Main HUD views
    private let hudOverlay = PassthroughContainerView()
    private let lookControl = LookControlView(startFraction: 0.42)
    private let weaponArt = ToolArtControlView(firstPerson: true)

    private let headingBox = UIControl()
    private let titleLabel = UILabel()
    private let coordinatesLabel = UILabel()
    private let noticeLabel = UILabel()
    private let crosshairLabel = UILabel()
    private let toastLabel = UILabel()

    private let joystick = StickControlView()
    private let pauseButton = HudActionButton(caption: "", symbol: "pause")
    private let jumpButton = HudActionButton(caption: loc("action_jump"), symbol: "jump")
    private let crouchButton = HudActionButton(caption: loc("action_crouch"), symbol: "crouch")
    /// Map mode with the Quake 2 game: held fire (commands 51 / 52).
    private let fireButton = HudActionButton(caption: loc("action_fire"), symbol: "fire")
    // Quake 2 (map mode with the game): help computer and inventory; while the
    // inventory is open, previous / next / use. The game draws both screens.
    private let helpButton = HudActionButton(caption: loc("action_help"), symbol: "help")
    private let inventoryButton = HudActionButton(caption: loc("action_inventory"), symbol: "backpack")
    private let inventoryPrevButton = HudActionButton(caption: "", symbol: "left")
    private let inventoryNextButton = HudActionButton(caption: "", symbol: "right")
    private let inventoryUseButton = HudActionButton(caption: loc("action_inventory_use"), symbol: "check")
    private var inventoryOpen = false

    private let switchWeaponButton = ToolSwitchButton()
    private let catalogButton = HudActionButton(caption: "", symbol: "cube")
    private let removeButton = HudActionButton(caption: "", symbol: "remove")
    private let toolWheel = ToolWheelOverlayView()

    // Bottom tool action strips
    private let spawnStrip = PassthroughContainerView()
    private let buildPlaceButton = HudActionButton(caption: "", symbol: "build")
    private let turnLeftButton = HudActionButton(caption: "", symbol: "left")
    private let turnRightButton = HudActionButton(caption: "", symbol: "right")

    private let gravityStrip = PassthroughContainerView()
    private let grabButton = HudActionButton(caption: "", symbol: "grab")
    private let freezeButton = HudActionButton(caption: loc("action_freeze"), symbol: "freeze")
    private let rotateHeldButton = HudActionButton(caption: loc("action_rotate_held"), symbol: "rotate")

    private let remoteStrip = PassthroughContainerView()
    private let returnDroneButton = HudActionButton(caption: loc("action_drone_home"), symbol: "home")
    private let putAwayRemoteButton = HudActionButton(caption: loc("action_put_away_remote"), symbol: "remove")

    private let interactionButton = HudActionButton(caption: loc("interaction_open"), symbol: "grab")
    private let dronePlaceButton = HudActionButton(caption: loc("drone_place"), symbol: "build")

    // Flight altitude controls
    private let altitudeContainer = PassthroughContainerView()
    private let flyUpButton = UIButton(type: .system)
    private let flyDownButton = UIButton(type: .system)

    // Catalog popup & FPV overlay
    private var catalogPopup: BuildCatalogPopupView?
    private let fpvOverlay = FpvFlightOverlayView()

    // Loading & Intro video overlay
    private let loadingOverlay = UIView()
    private let loadingTitleLabel = UILabel()
    private let loadingSubtitleLabel = UILabel()
    private let loadingProgressView = UIProgressView(progressViewStyle: .default)
    private let loadingStatusLabel = UILabel()

    private let introVideoContainer = UIView()
    private var introPlayer: AVPlayer?
    private var introPlayerLayer: AVPlayerLayer?
    private let skipIntroButton = UIButton(type: .system)
    private var introFinished = false

    // Gameplay UI state
    private let toolsState = SandboxToolState()
    private var requestedTool: SandboxHandTool?
    private var pendingSelection: Int?
    private var selectedCatalogIndex: Int = 0
    private var selectedSurface: Int = 1
    private var interactionCode: Int = 0
    private var droneState: Int = 0
    private var wasInFpvMode = false
    private var worldReady = false
    private var appActive = true
    private var isPausedMenu = false
    private var flyingNow = false
    private var latestPosition: [Float] = [0.0, 1.7, 0.0]
    private var latestYawDegrees: Float = 0
    /// Realms map mode (status slot 17): the sandbox HUD steps aside while a map is walked.
    private var mapMode = false
    private var mapTexturesLoading = false
    private var mapName = ""
    /// The Quake 2 game runs the map (status slot 18); health, armor, ammo (19...21).
    private var mapGame = false
    private var mapStats: [Int32] = [0, 0, 0]

    private var pollTimer: Timer?
    private var toastHideWorkItem: DispatchWorkItem?

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }
    override var preferredInterfaceOrientationForPresentation: UIInterfaceOrientation { .landscapeRight }

    override func viewDidLoad() {
        super.viewDidLoad()
        UIApplication.shared.isIdleTimerDisabled = true

        guard let metalView = self.view as? MTKView else {
            return
        }
        mtkView = metalView
        mtkView.isMultipleTouchEnabled = true
        mtkView.backgroundColor = .black

        if let defaultDevice = MTLCreateSystemDefaultDevice() {
            mtkView.device = defaultDevice
            if let sandboxBridge = SandboxBridge(metalKitView: mtkView) {
                bridge = sandboxBridge
                mtkView.delegate = sandboxBridge
            }
        }

        online.onPlayers = { [weak self] values in
            values.withUnsafeBufferPointer { buffer in
                self?.bridge?.setRemotePlayers(buffer.baseAddress, count: buffer.count)
            }
        }
        online.onStatus = { [weak self] message in self?.showToast(message) }
        setupHud()
        setupLoadingAndIntro()
        registerLifecycleNotifications()

        if bridge == nil {
            finishIntroImmediately()
            loadingProgressView.isHidden = true
            loadingStatusLabel.text = loc("metal_unavailable")
        }

        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.08, repeats: true) { [weak self] _ in
            self?.pollNativeState()
        }
    }

    deinit {
        pollTimer?.invalidate()
        NotificationCenter.default.removeObserver(self)
    }

    private func registerLifecycleNotifications() {
        let nc = NotificationCenter.default
        nc.addObserver(self, selector: #selector(handleWillResignActive), name: UIApplication.willResignActiveNotification, object: nil)
        nc.addObserver(self, selector: #selector(handleDidEnterBackground), name: UIApplication.didEnterBackgroundNotification, object: nil)
        nc.addObserver(self, selector: #selector(handleDidBecomeActive), name: UIApplication.didBecomeActiveNotification, object: nil)
        nc.addObserver(self, selector: #selector(handleWillTerminate), name: UIApplication.willTerminateNotification, object: nil)
    }

    @objc private func handleWillResignActive() {
        appActive = false
        resetTransientInputs()
        bridge?.setGameplayReady(false)
        introPlayer?.pause()
    }

    @objc private func handleDidEnterBackground() {
        online.disconnect()
        appActive = false
        resetTransientInputs()
        bridge?.setGameplayReady(false)
        bridge?.flushSave()
        bridge?.setAudioActive(false)
        mtkView?.isPaused = true
    }

    @objc private func handleDidBecomeActive() {
        appActive = true
        bridge?.setAudioActive(true)
        mtkView?.isPaused = false
        if !introFinished {
            introPlayer?.play()
        }
        pollNativeState()
    }

    @objc private func handleWillTerminate() {
        bridge?.flushSave()
    }

    private func resetTransientInputs() {
        toolWheel.cancel()
        fpvOverlay.reset()
        joystick.reset()
        lookControl.reset()
        bridge?.setVerticalInput(0)
        bridge?.resetInput()
    }

    // MARK: - HUD Setup

    private func setupHud() {
        hudOverlay.frame = view.bounds
        hudOverlay.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        hudOverlay.isMultipleTouchEnabled = true
        view.addSubview(hudOverlay)

        lookControl.onLook = { [weak self] dx, dy in
            self?.bridge?.addLookDeltaX(dx, deltaY: dy)
        }
        hudOverlay.addSubview(lookControl)

        weaponArt.isHidden = true
        hudOverlay.addSubview(weaponArt)

        // Top-left heading card
        headingBox.backgroundColor = UIColor(red: 0.15, green: 0.19, blue: 0.22, alpha: 0.68)
        headingBox.layer.cornerRadius = 6
        headingBox.addTarget(self, action: #selector(copyCoordinatesToClipboard), for: .touchUpInside)
        hudOverlay.addSubview(headingBox)

        titleLabel.text = loc("hud_title")
        titleLabel.font = UIFont.systemFont(ofSize: 13, weight: .bold)
        titleLabel.textColor = .white
        titleLabel.isUserInteractionEnabled = false
        headingBox.addSubview(titleLabel)

        coordinatesLabel.text = "X: —   Y: —   Z: —"
        coordinatesLabel.font = UIFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium)
        coordinatesLabel.textColor = .white
        coordinatesLabel.isUserInteractionEnabled = false
        headingBox.addSubview(coordinatesLabel)

        noticeLabel.text = loc("notice_free_mode")
        noticeLabel.font = UIFont.systemFont(ofSize: 10.5, weight: .regular)
        noticeLabel.textColor = UIColor(white: 0.9, alpha: 0.95)
        noticeLabel.numberOfLines = 2
        noticeLabel.isUserInteractionEnabled = false
        headingBox.addSubview(noticeLabel)

        crosshairLabel.text = "+"
        crosshairLabel.font = UIFont.systemFont(ofSize: 24, weight: .light)
        crosshairLabel.textColor = .white
        crosshairLabel.textAlignment = .center
        crosshairLabel.isUserInteractionEnabled = false
        hudOverlay.addSubview(crosshairLabel)

        // Movement joystick
        joystick.onMove = { [weak self] right, forward in
            guard let self = self else { return }
            self.weaponArt.moving = hypot(right, forward) > 0.1
            self.bridge?.setMoveRight(right, forward: forward)
        }
        hudOverlay.addSubview(joystick)

        // Top-right controls
        pauseButton.visualScale = 0.73
        pauseButton.accessibilityLabel = loc("pause")
        pauseButton.onTap = { [weak self] in
            self?.showPauseMenu()
        }
        hudOverlay.addSubview(pauseButton)

        switchWeaponButton.onWheelBegin = { [weak self] in
            guard let self = self, self.worldReady, self.appActive, !self.isPausedMenu, !self.mapMode else { return }
            self.closeCatalogPopup()
            self.lookControl.reset()
            self.hudOverlay.bringSubviewToFront(self.toolWheel)
            self.toolWheel.beginSelection(current: self.toolsState.tool)
        }
        switchWeaponButton.onWheelDrag = { [weak self] dx, dy in
            self?.toolWheel.updateDrag(dx: dx, dy: dy)
        }
        switchWeaponButton.onWheelEnd = { [weak self] in
            self?.toolWheel.finishSelection() ?? false
        }
        switchWeaponButton.onWheelCancel = { [weak self] in
            self?.toolWheel.cancel()
        }
        switchWeaponButton.onQuickTap = { [weak self] in
            self?.showToolActionSheet()
        }
        hudOverlay.addSubview(switchWeaponButton)

        catalogButton.visualScale = 0.73
        catalogButton.accessibilityLabel = loc("catalog_open_description")
        catalogButton.onTap = { [weak self] in
            self?.toggleCatalogPopup()
        }
        hudOverlay.addSubview(catalogButton)

        removeButton.visualScale = 0.73
        removeButton.danger = true
        removeButton.accessibilityLabel = loc("action_remove")
        removeButton.onTap = { [weak self] in
            self?.bridge?.sendCommand(4)
        }
        hudOverlay.addSubview(removeButton)

        // Right-bottom jump & crouch
        jumpButton.fireOnPress = true
        jumpButton.onTap = { [weak self] in
            self?.bridge?.sendCommand(8)
        }
        hudOverlay.addSubview(jumpButton)

        crouchButton.onTap = { [weak self] in
            guard let self = self else { return }
            self.crouchButton.isSelected.toggle()
            self.bridge?.sendCommand(9)
        }
        hudOverlay.addSubview(crouchButton)

        fireButton.isHidden = true
        fireButton.onPress = { [weak self] in
            self?.bridge?.sendCommand(51)
        }
        fireButton.onRelease = { [weak self] in
            self?.bridge?.sendCommand(52)
        }
        hudOverlay.addSubview(fireButton)

        // Quake 2's help computer and inventory: commands 53...57 ("help", "inven", "invprev", "invnext", "invuse")
        inventoryPrevButton.accessibilityLabel = loc("action_inventory_previous")
        inventoryNextButton.accessibilityLabel = loc("action_inventory_next")
        for (button, command) in [(helpButton, 53), (inventoryButton, 54), (inventoryPrevButton, 55),
                                  (inventoryNextButton, 56), (inventoryUseButton, 57)] as [(HudActionButton, Int32)] {
            button.visualScale = 0.8
            button.isHidden = true
            button.onTap = { [weak self] in
                self?.bridge?.sendCommand(command)
            }
            hudOverlay.addSubview(button)
        }

        // Spawn strip
        buildPlaceButton.accessibilityLabel = loc("action_place_selected")
        buildPlaceButton.onTap = { [weak self] in
            guard let self = self, self.requestedTool == nil else { return }
            let nativeType = SandboxBuildCatalog.entries[self.selectedCatalogIndex].nativeType
            self.bridge?.sendCommand(200 + nativeType)
        }
        turnLeftButton.accessibilityLabel = loc("action_turn_left")
        turnLeftButton.onTap = { [weak self] in self?.bridge?.sendCommand(31) }
        turnRightButton.accessibilityLabel = loc("action_turn_right")
        turnRightButton.onTap = { [weak self] in self?.bridge?.sendCommand(2) }
        spawnStrip.addSubview(buildPlaceButton)
        spawnStrip.addSubview(turnLeftButton)
        spawnStrip.addSubview(turnRightButton)
        hudOverlay.addSubview(spawnStrip)

        // Gravity strip
        grabButton.onTap = { [weak self] in self?.bridge?.sendCommand(32) }
        freezeButton.onTap = { [weak self] in self?.bridge?.sendCommand(38) }
        rotateHeldButton.onTap = { [weak self] in self?.bridge?.sendCommand(33) }
        gravityStrip.addSubview(grabButton)
        gravityStrip.addSubview(freezeButton)
        gravityStrip.addSubview(rotateHeldButton)
        hudOverlay.addSubview(gravityStrip)

        // Remote strip
        returnDroneButton.onTap = { [weak self] in self?.bridge?.sendCommand(44) }
        putAwayRemoteButton.onTap = { [weak self] in self?.bridge?.sendCommand(42) }
        remoteStrip.addSubview(returnDroneButton)
        remoteStrip.addSubview(putAwayRemoteButton)
        hudOverlay.addSubview(remoteStrip)

        // Contextual interaction & drone placement
        interactionButton.isHidden = true
        interactionButton.onTap = { [weak self] in
            guard let self = self else { return }
            self.bridge?.sendCommand(self.interactionCode == 6 ? 43 : 36)
        }
        hudOverlay.addSubview(interactionButton)

        dronePlaceButton.isHidden = true
        dronePlaceButton.onTap = { [weak self] in
            self?.bridge?.sendCommand(41)
        }
        hudOverlay.addSubview(dronePlaceButton)

        // Noclip altitude controls
        configureAltitudeButton(flyUpButton, title: loc("fly_up"), upAction: #selector(handleFlyUpDown), releaseAction: #selector(handleFlyRelease))
        configureAltitudeButton(flyDownButton, title: loc("fly_down"), upAction: #selector(handleFlyDownDown), releaseAction: #selector(handleFlyRelease))
        altitudeContainer.addSubview(flyUpButton)
        altitudeContainer.addSubview(flyDownButton)
        altitudeContainer.isHidden = true
        hudOverlay.addSubview(altitudeContainer)

        // Tool wheel
        toolWheel.onChoose = { [weak self] chosen in
            self?.selectTool(chosen)
        }
        hudOverlay.addSubview(toolWheel)

        // FPV overlay
        fpvOverlay.isHidden = true
        fpvOverlay.onDroneInput = { [weak self] throttle, yaw, pitch, roll in
            self?.bridge?.setDroneThrottle(throttle, yaw: yaw, pitch: pitch, roll: roll)
        }
        fpvOverlay.onLook = { [weak self] dx, dy in
            self?.bridge?.addLookDeltaX(dx, deltaY: dy)
        }
        fpvOverlay.onToggleView = { [weak self] in
            self?.bridge?.sendCommand(45)
        }
        fpvOverlay.onExitFpv = { [weak self] in
            self?.bridge?.sendCommand(42)
        }
        hudOverlay.addSubview(fpvOverlay)

        // Toast label
        toastLabel.backgroundColor = UIColor(white: 0.08, alpha: 0.82)
        toastLabel.textColor = .white
        toastLabel.font = UIFont.systemFont(ofSize: 12.5, weight: .medium)
        toastLabel.textAlignment = .center
        toastLabel.layer.cornerRadius = 8
        toastLabel.clipsToBounds = true
        toastLabel.isHidden = true
        toastLabel.isUserInteractionEnabled = false
        hudOverlay.addSubview(toastLabel)

        renderToolControls()
    }

    private func configureAltitudeButton(_ btn: UIButton, title: String, upAction: Selector, releaseAction: Selector) {
        btn.setTitle(title, for: .normal)
        btn.setTitleColor(.white, for: .normal)
        btn.titleLabel?.font = UIFont.systemFont(ofSize: 12, weight: .semibold)
        btn.backgroundColor = UIColor(red: 0.16, green: 0.22, blue: 0.26, alpha: 0.78)
        btn.layer.cornerRadius = 8
        btn.layer.borderWidth = 1
        btn.layer.borderColor = UIColor(white: 1.0, alpha: 0.35).cgColor
        btn.addTarget(self, action: upAction, for: .touchDown)
        btn.addTarget(self, action: releaseAction, for: [.touchUpInside, .touchUpOutside, .touchCancel])
    }

    @objc private func handleFlyUpDown() { bridge?.setVerticalInput(1.0) }
    @objc private func handleFlyDownDown() { bridge?.setVerticalInput(-1.0) }
    @objc private func handleFlyRelease() { bridge?.setVerticalInput(0.0) }

    // MARK: - Loading & Intro Video

    private func setupLoadingAndIntro() {
        loadingOverlay.frame = view.bounds
        loadingOverlay.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        loadingOverlay.backgroundColor = UIColor(red: 24 / 255, green: 35 / 255, blue: 42 / 255, alpha: 1.0)
        view.addSubview(loadingOverlay)

        loadingTitleLabel.text = loc("loading_title")
        loadingTitleLabel.font = UIFont.systemFont(ofSize: 32, weight: .bold)
        loadingTitleLabel.textColor = .white
        loadingTitleLabel.textAlignment = .center
        loadingOverlay.addSubview(loadingTitleLabel)

        loadingSubtitleLabel.text = loc("loading_subtitle")
        loadingSubtitleLabel.font = UIFont.systemFont(ofSize: 14, weight: .regular)
        loadingSubtitleLabel.textColor = UIColor(white: 0.85, alpha: 1.0)
        loadingSubtitleLabel.textAlignment = .center
        loadingOverlay.addSubview(loadingSubtitleLabel)

        loadingProgressView.progressTintColor = UIColor(red: 0.35, green: 0.86, blue: 0.70, alpha: 1.0)
        loadingProgressView.trackTintColor = UIColor(white: 1.0, alpha: 0.18)
        loadingOverlay.addSubview(loadingProgressView)

        loadingStatusLabel.text = loc("loading_preparing_world")
        loadingStatusLabel.font = UIFont.systemFont(ofSize: 14, weight: .medium)
        loadingStatusLabel.textColor = .white
        loadingStatusLabel.textAlignment = .center
        loadingStatusLabel.numberOfLines = 3
        loadingOverlay.addSubview(loadingStatusLabel)

        if let root = Bundle.main.resourcePath {
            let videoPath = (root as NSString).appendingPathComponent("intro.mp4")
            if FileManager.default.fileExists(atPath: videoPath) {
                let url = URL(fileURLWithPath: videoPath)
                let item = AVPlayerItem(url: url)
                let player = AVPlayer(playerItem: item)
                introPlayer = player

                introVideoContainer.frame = view.bounds
                introVideoContainer.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                introVideoContainer.backgroundColor = .black
                view.addSubview(introVideoContainer)

                let layer = AVPlayerLayer(player: player)
                layer.videoGravity = .resizeAspectFill
                layer.frame = introVideoContainer.bounds
                introVideoContainer.layer.addSublayer(layer)
                introPlayerLayer = layer

                skipIntroButton.setTitle(loc("skip_intro"), for: .normal)
                skipIntroButton.setTitleColor(.white, for: .normal)
                skipIntroButton.titleLabel?.font = UIFont.systemFont(ofSize: 13, weight: .semibold)
                skipIntroButton.backgroundColor = UIColor(white: 0.1, alpha: 0.65)
                skipIntroButton.layer.cornerRadius = 8
                skipIntroButton.addTarget(self, action: #selector(finishIntroImmediately), for: .touchUpInside)
                introVideoContainer.addSubview(skipIntroButton)

                NotificationCenter.default.addObserver(self,
                                                       selector: #selector(finishIntroImmediately),
                                                       name: .AVPlayerItemDidPlayToEndTime,
                                                       object: item)
                NotificationCenter.default.addObserver(self,
                                                       selector: #selector(finishIntroImmediately),
                                                       name: .AVPlayerItemFailedToPlayToEndTime,
                                                       object: item)
                player.play()
                return
            }
        }
        introFinished = true
    }

    @objc private func finishIntroImmediately() {
        guard !introFinished else { return }
        introFinished = true
        introPlayer?.pause()
        introPlayer = nil
        introPlayerLayer?.removeFromSuperlayer()
        introPlayerLayer = nil
        introVideoContainer.removeFromSuperview()
    }

    // MARK: - Layout

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        let b = view.bounds
        let safe = view.safeAreaInsets

        lookControl.frame = b
        toolWheel.frame = b
        fpvOverlay.frame = b
        introPlayerLayer?.frame = introVideoContainer.bounds
        skipIntroButton.frame = CGRect(x: b.width - max(16, safe.right + 12) - 122,
                                       y: max(14, safe.top + 10),
                                       width: 122,
                                       height: 36)

        // Loading screen layout
        loadingTitleLabel.frame = CGRect(x: 20, y: b.midY - 76, width: b.width - 40, height: 40)
        loadingSubtitleLabel.frame = CGRect(x: 20, y: b.midY - 32, width: b.width - 40, height: 22)
        loadingProgressView.frame = CGRect(x: b.midX - 130, y: b.midY + 6, width: 260, height: 8)
        loadingStatusLabel.frame = CGRect(x: 30, y: b.midY + 24, width: b.width - 60, height: 64)

        let leftMargin = max(14.0, safe.left + 8.0)
        let rightMargin = max(10.0, safe.right + 8.0)
        let topMargin = max(8.0, safe.top + 6.0)
        let bottomMargin = max(10.0, safe.bottom + 6.0)

        headingBox.frame = CGRect(x: leftMargin, y: topMargin, width: 248, height: 68)
        titleLabel.frame = CGRect(x: 10, y: 6, width: 228, height: 18)
        coordinatesLabel.frame = CGRect(x: 10, y: 25, width: 228, height: 17)
        noticeLabel.frame = CGRect(x: 10, y: 42, width: 228, height: 22)

        crosshairLabel.frame = CGRect(x: b.midX - 16, y: b.midY - 16, width: 32, height: 32)

        let stickSize = min(176.0, max(120.0, b.height * 0.34))
        joystick.frame = CGRect(x: leftMargin,
                                y: b.height - stickSize - max(18.0, bottomMargin + 8.0),
                                width: stickSize,
                                height: stickSize)

        let actionSize = min(64.0, max(56.0, b.height * 0.145))
        let rightX = b.width - rightMargin - 60.0
        pauseButton.frame = CGRect(x: rightX, y: topMargin, width: 60, height: 60)
        helpButton.frame = CGRect(x: rightX - 128, y: topMargin, width: 60, height: 60)
        inventoryButton.frame = CGRect(x: rightX - 64, y: topMargin, width: 60, height: 60)
        inventoryPrevButton.frame = CGRect(x: rightX - 128, y: topMargin + 64, width: 60, height: 60)
        inventoryNextButton.frame = CGRect(x: rightX - 64, y: topMargin + 64, width: 60, height: 60)
        inventoryUseButton.frame = CGRect(x: rightX, y: topMargin + 64, width: 60, height: 60)
        switchWeaponButton.frame = CGRect(x: rightX, y: topMargin + 60, width: 60, height: 60)
        catalogButton.frame = CGRect(x: rightX - 64, y: topMargin + 60, width: 60, height: 60)
        removeButton.frame = CGRect(x: rightX - 64, y: topMargin + 120, width: 60, height: 60)

        let moveX = b.width - rightMargin - actionSize
        crouchButton.frame = CGRect(x: moveX, y: b.height - bottomMargin - actionSize, width: actionSize, height: actionSize)
        jumpButton.frame = CGRect(x: moveX, y: crouchButton.frame.minY - actionSize - 4, width: actionSize, height: actionSize)
        fireButton.frame = CGRect(x: moveX, y: jumpButton.frame.minY - actionSize - 4, width: actionSize, height: actionSize)

        weaponArt.frame = CGRect(x: b.width - rightMargin - 315, y: b.height - bottomMargin - 195, width: 200, height: 125)

        // Bottom center strips
        let btnW: CGFloat = 72
        let btnH: CGFloat = 62
        let stripY = b.height - bottomMargin - btnH

        let spawnW = btnW * 3 + 8
        spawnStrip.frame = CGRect(x: b.midX - spawnW * 0.5, y: stripY, width: spawnW, height: btnH)
        buildPlaceButton.frame = CGRect(x: 0, y: 0, width: btnW, height: btnH)
        turnLeftButton.frame = CGRect(x: btnW + 4, y: 0, width: btnW, height: btnH)
        turnRightButton.frame = CGRect(x: (btnW + 4) * 2, y: 0, width: btnW, height: btnH)

        let gravCount: CGFloat = toolsState.rotateHeldVisible ? 3 : 2
        let gravW = btnW * gravCount + (gravCount - 1) * 4
        gravityStrip.frame = CGRect(x: b.midX - gravW * 0.5, y: stripY, width: gravW, height: btnH)
        grabButton.frame = CGRect(x: 0, y: 0, width: btnW, height: btnH)
        freezeButton.frame = CGRect(x: btnW + 4, y: 0, width: btnW, height: btnH)
        rotateHeldButton.frame = CGRect(x: (btnW + 4) * 2, y: 0, width: btnW, height: btnH)

        let remW: CGFloat = 84 * 2 + 6
        remoteStrip.frame = CGRect(x: b.midX - remW * 0.5, y: stripY, width: remW, height: btnH)
        returnDroneButton.frame = CGRect(x: 0, y: 0, width: 84, height: btnH)
        putAwayRemoteButton.frame = CGRect(x: 90, y: 0, width: 84, height: btnH)

        interactionButton.frame = CGRect(x: moveX - 88, y: b.height - bottomMargin - 60, width: 82, height: 60)
        dronePlaceButton.frame = CGRect(x: b.midX - 48, y: b.height - bottomMargin - 70, width: 96, height: 68)

        altitudeContainer.frame = CGRect(x: moveX - 94, y: b.midY - 52, width: 84, height: 104)
        flyUpButton.frame = CGRect(x: 0, y: 0, width: 84, height: 48)
        flyDownButton.frame = CGRect(x: 0, y: 56, width: 84, height: 48)

        if let popup = catalogPopup {
            let popLeft = leftMargin + stickSize + 18
            let popWidth = max(280, b.width - popLeft - rightMargin - 134)
            let popHeight = max(180, b.height - topMargin - bottomMargin - 76)
            popup.frame = CGRect(x: popLeft, y: topMargin + 54, width: popWidth, height: popHeight)
        }

        toastLabel.frame = CGRect(x: b.midX - 180, y: topMargin + 8, width: 360, height: 34)
    }

    // MARK: - Polling & State Sync

    private func pollNativeState() {
        weaponArt.tickAnimation()
        guard let bridge = bridge else { return }

        var state = [Float](repeating: 0, count: 23)
        let count = state.withUnsafeMutableBufferPointer { buf -> Int in
            guard let base = buf.baseAddress else { return 0 }
            return Int(bridge.pollStatus(base, count: 23))
        }
        guard count >= 14 else { return }
        let nowMap = count >= 18 && state[17] > 0.5
        mapTexturesLoading = count >= 18 && state[17] > 1.5
        // Re-read every poll: picking another map keeps map mode on.
        if nowMap {
            let name = (bridge.currentMapPath() as NSString).lastPathComponent
            if !name.isEmpty { mapName = name }
        }
        let nowGame = nowMap && count >= 22 && state[18] > 0.5
        if nowGame {
            mapStats = [Int32(state[19]), Int32(state[20]), Int32(state[21])]
        }
        if nowGame != mapGame {
            mapGame = nowGame
            if !nowGame && fireButton.isHighlighted {
                fireButton.cancelTracking(with: nil)
                bridge.sendCommand(52)
            }
            fireButton.isHidden = !nowGame || wasInFpvMode
            helpButton.isHidden = !nowGame || wasInFpvMode
            inventoryButton.isHidden = !nowGame || wasInFpvMode
        }
        // status slot 22: bit 1 = the game's inventory is open
        let nowInventory = nowGame && count >= 23 && (Int(state[22]) & 2) != 0
        if nowInventory != inventoryOpen {
            inventoryOpen = nowInventory
            for button in [inventoryPrevButton, inventoryNextButton, inventoryUseButton] {
                button.isHidden = !nowInventory || wasInFpvMode
            }
        }
        if nowMap != mapMode {
            mapMode = nowMap
            if nowMap {
                closeCatalogPopup()
                toolWheel.cancel()
            } else {
                mapName = ""
            }
        }
        if count >= 17 { online.updatePose(state) }
        if count >= 15 { crouchButton.isSelected = Int(state[14]) == 1 }
        if count >= 16 { latestYawDegrees = state[15] * 180.0 / .pi }

        let confirmedTool = SandboxHandTool(rawValue: Int(state[7])) ?? .handEmpty
        if requestedTool == confirmedTool {
            requestedTool = nil
        }
        if requestedTool == nil && confirmedTool != toolsState.tool {
            toolsState.select(confirmedTool)
        }
        if confirmedTool == toolsState.tool {
            toolsState.confirmHolding(state[6] > 0.5)
        }

        let stage = Int(state[4])
        if stage == 6 {
            let nativeType = Int32(state[9])
            let nativeSurf = Int(state[10])
            if let idx = SandboxBuildCatalog.entries.firstIndex(where: { $0.nativeType == nativeType }) {
                if pendingSelection == idx {
                    pendingSelection = nil
                }
                if pendingSelection == nil {
                    selectedCatalogIndex = idx
                    selectedSurface = SandboxBuildCatalog.entries[idx].defaultSurface
                }
            }
            if [1, 2, 3, 5, 6].contains(nativeSurf) {
                selectedSurface = nativeSurf
            }
        }

        interactionCode = Int(state[11])
        interactionButton.isHidden = (interactionCode <= 0) || wasInFpvMode
        switch interactionCode {
        case 6: interactionButton.caption = loc("interaction_connect")
        case 1: interactionButton.caption = loc("interaction_open")
        case 2: interactionButton.caption = loc("interaction_close")
        case 3: interactionButton.caption = loc("interaction_sit")
        case 4: interactionButton.caption = loc("interaction_exit")
        default: interactionButton.caption = ""
        }

        freezeButton.caption = state[12] > 0.5 ? loc("action_unfreeze") : loc("action_freeze")

        latestPosition = [state[0], state[1], state[2]]
        coordinatesLabel.text = String(format: "X: %.1f   Y: %.1f   Z: %.1f", state[0], state[1], state[2])

        flyingNow = state[3] > 0.5
        altitudeContainer.isHidden = !flyingNow || wasInFpvMode

        worldReady = (stage == 6) && introFinished
        let unlocked = worldReady && appActive && !isPausedMenu
        bridge.setGameplayReady(unlocked)
        weaponArt.running = unlocked

        loadingOverlay.isHidden = (worldReady && appActive)
        loadingProgressView.isHidden = stage < 0
        let pct = max(0, min(100, Int(state[8])))
        loadingProgressView.progress = Float(pct) / 100.0

        let stageText: String
        switch stage {
        case 0: stageText = loc("loading_stage_graphics")
        case 1: stageText = loc("loading_stage_textures")
        case 2: stageText = loc("loading_stage_city")
        case 3: stageText = loc("loading_stage_vehicle")
        case 4: stageText = loc("loading_stage_gpu")
        case 5: stageText = loc("loading_stage_first_frame")
        case 6: stageText = loc("loading_stage_ready")
        default: stageText = loc("loading_failed_resources")
        }
        loadingStatusLabel.text = (0...5).contains(stage) ? "\(stageText) \(pct)%" : stageText

        // in map mode only the quick save / load results (8...10, a few seconds)
        let noticeCode = mapMode && !(8...10).contains(Int(state[5])) ? 0 : Int(state[5])
        switch noticeCode {
        case 8: noticeLabel.text = loc("notice_game_saved")
        case 9: noticeLabel.text = loc("notice_save_failed")
        case 10: noticeLabel.text = loc("notice_no_quick_save")
        case 7: noticeLabel.text = loc("notice_map_failed")
        case 1: noticeLabel.text = loc("notice_spawn_moved_short")
        case 2: noticeLabel.text = loc("notice_spawn_unavailable_short")
        case 3: noticeLabel.text = loc("notice_no_parking_short")
        case 4: noticeLabel.text = loc("notice_player_respawned")
        case 5: noticeLabel.text = loc("notice_no_exit")
        default:
            if flyingNow {
                noticeLabel.text = loc("notice_flying")
            } else if mapMode {
                if mapGame {
                    // health, armor and ammo are on the game's own status bar
                    noticeLabel.text = locF("notice_map_game", mapName)
                } else {
                    noticeLabel.text = mapTexturesLoading ? locF("notice_map_textures", mapName) : locF("notice_map_mode", mapName)
                }
            } else {
                switch toolsState.tool {
                case .handEmpty:
                    noticeLabel.text = loc("tool_empty_hands")
                case .spawnGun:
                    noticeLabel.text = locF("notice_spawn_gun", SandboxBuildCatalog.entries[selectedCatalogIndex].label)
                case .gravityGun:
                    noticeLabel.text = toolsState.holding ? loc("notice_item_grabbed") : loc("notice_gravity_gun_short")
                case .deleteTool:
                    noticeLabel.text = loc("notice_delete_tool")
                case .droneRemote:
                    noticeLabel.text = loc("notice_fpv_remote")
                }
            }
        }

        droneState = Int(state[13])
        let inFpv = (3...5).contains(droneState)
        if inFpv != wasInFpvMode {
            wasInFpvMode = inFpv
            if inFpv {
                closeCatalogPopup()
                toolWheel.cancel()
                joystick.reset()
                lookControl.reset()
                showToast(loc("fpv_controls_hint"))
            } else {
                fpvOverlay.reset()
            }
        }
        fpvOverlay.isHidden = !inFpv
        setNormalHudHidden(inFpv)
        if !inFpv {
            renderToolControls()
            dronePlaceButton.isHidden = (droneState != 1)
        }
    }

    private func setNormalHudHidden(_ hidden: Bool) {
        let normalViews: [UIView] = [
            lookControl, weaponArt, headingBox, crosshairLabel, joystick,
            pauseButton, switchWeaponButton, catalogButton, removeButton,
            jumpButton, crouchButton, fireButton, helpButton, inventoryButton, inventoryPrevButton,
            inventoryNextButton, inventoryUseButton, spawnStrip, gravityStrip, remoteStrip,
            interactionButton, dronePlaceButton, altitudeContainer
        ]
        if hidden {
            for v in normalViews { v.isHidden = true }
        } else {
            lookControl.isHidden = false
            headingBox.isHidden = false
            crosshairLabel.isHidden = false
            joystick.isHidden = false
            pauseButton.isHidden = false
            switchWeaponButton.isHidden = mapMode
            jumpButton.isHidden = false
            crouchButton.isHidden = false
            fireButton.isHidden = !mapGame
            helpButton.isHidden = !mapGame
            inventoryButton.isHidden = !mapGame
            for button in [inventoryPrevButton, inventoryNextButton, inventoryUseButton] {
                button.isHidden = !inventoryOpen
            }
            altitudeContainer.isHidden = !flyingNow
        }
    }

    private func renderToolControls() {
        guard !wasInFpvMode else { return }
        // Seated in a car or walking a Realms map: no sandbox tools.
        let seated = (interactionCode == 4) || mapMode
        switchWeaponButton.isHidden = mapMode
        weaponArt.tool = toolsState.tool
        weaponArt.isHidden = !(toolsState.viewmodelVisible && !seated)

        catalogButton.isHidden = seated
        spawnStrip.isHidden = !(toolsState.spawnVisible && !seated)
        gravityStrip.isHidden = !(toolsState.gravityVisible && !seated)
        remoteStrip.isHidden = !(toolsState.tool == .droneRemote && !seated)
        removeButton.isHidden = !(toolsState.deleteVisible && !seated)
        rotateHeldButton.isHidden = !(toolsState.rotateHeldVisible && !seated)

        grabButton.symbol = toolsState.holding ? "release" : "grab"
        grabButton.accessibilityLabel = toolsState.grabCaption
        view.setNeedsLayout()
    }

    // MARK: - User Actions

    private func selectTool(_ tool: SandboxHandTool) {
        closeCatalogPopup()
        requestedTool = tool
        toolsState.select(tool)
        bridge?.sendCommand(Int32(20 + tool.rawValue))
        renderToolControls()
    }

    private func showToolActionSheet() {
        guard worldReady, appActive, !isPausedMenu, !mapMode else { return }
        setMenuPaused(true)
        let alert = UIAlertController(title: loc("tool_picker_title"), message: nil, preferredStyle: .alert)
        for (idx, title) in SandboxToolWheelMath.names.enumerated() {
            alert.addAction(UIAlertAction(title: title, style: .default) { [weak self] _ in
                self?.setMenuPaused(false)
                self?.selectTool(SandboxToolWheelMath.tools[idx])
            })
        }
        alert.addAction(UIAlertAction(title: loc("cancel"), style: .cancel) { [weak self] _ in
            self?.setMenuPaused(false, sound: UiSound.close)
        })
        present(alert, animated: true)
    }

    private func toggleCatalogPopup() {
        if catalogPopup != nil {
            closeCatalogPopup(sound: true)
            return
        }
        guard worldReady, appActive, !isPausedMenu, interactionCode != 4, !mapMode else { return }
        let popup = BuildCatalogPopupView()
        popup.selectedCatalogIndex = selectedCatalogIndex
        popup.onClose = { [weak self] in
            self?.closeCatalogPopup(sound: true)
        }
        popup.onTab = { [weak self] in
            self?.bridge?.playUiSound(UiSound.click)
        }
        popup.onSelectCatalogItem = { [weak self] index in
            guard let self = self else { return }
            self.requestedTool = .spawnGun
            self.toolsState.select(.spawnGun)
            let entry = SandboxBuildCatalog.entries[index]
            self.bridge?.sendCommand(300 + entry.nativeType)
            self.pendingSelection = index
            self.selectedCatalogIndex = index
            self.selectedSurface = entry.defaultSurface
            self.bridge?.playUiSound(UiSound.click)
            self.closeCatalogPopup()
            self.renderToolControls()
        }
        popup.onTakeFpvRemote = { [weak self] in
            self?.bridge?.playUiSound(UiSound.click)
            self?.selectTool(.droneRemote)
        }
        popup.onTakeFpvDrone = { [weak self] in
            self?.bridge?.playUiSound(UiSound.click)
            self?.bridge?.sendCommand(40)
            self?.closeCatalogPopup()
        }
        hudOverlay.addSubview(popup)
        catalogPopup = popup
        bridge?.playUiSound(UiSound.open)
        view.setNeedsLayout()
    }

    private func closeCatalogPopup(sound: Bool = false) {
        if sound && catalogPopup != nil { bridge?.playUiSound(UiSound.close) }
        catalogPopup?.removeFromSuperview()
        catalogPopup = nil
    }

    /// Menu sounds through the shared mixer (SandboxBridge playUiSound).
    private enum UiSound {
        static let click: Int32 = 0, open: Int32 = 1, close: Int32 = 2, confirm: Int32 = 3, error: Int32 = 4
    }

    /// Dialogs open with `open`; a choice clicks unless `sound` says otherwise.
    private func setMenuPaused(_ value: Bool, sound: Int32? = nil) {
        bridge?.playUiSound(sound ?? (value ? UiSound.open : UiSound.click))
        toolWheel.cancel()
        closeCatalogPopup()
        isPausedMenu = value
        joystick.reset()
        lookControl.reset()
        bridge?.setVerticalInput(0)
        bridge?.setGameplayReady(worldReady && appActive && !isPausedMenu)
    }

    private func showPauseMenu() {
        guard worldReady else { return }
        if mapMode {
            showMapPauseMenu()
            return
        }
        setMenuPaused(true)
        let flyTitle = flyingNow ? loc("pause_flight_off") : loc("pause_flight_on")
        let alert = UIAlertController(title: loc("pause"), message: nil, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: loc("pause_resume"), style: .cancel) { [weak self] _ in
            self?.setMenuPaused(false, sound: UiSound.close)
        })
        alert.addAction(UIAlertAction(title: flyTitle, style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(6)
        })
        alert.addAction(UIAlertAction(title: loc("pause_road"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(7)
        })
        alert.addAction(UIAlertAction(title: loc("pause_restore_uaz"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(34)
        })
        alert.addAction(UIAlertAction(title: loc("pause_spawn"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(35)
        })
        alert.addAction(UIAlertAction(title: loc("pause_online"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { self?.showOnlineDialog() }
        })
        alert.addAction(UIAlertAction(title: loc("pause_disconnect"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.online.disconnect()
        })
        alert.addAction(UIAlertAction(title: loc("pause_language"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.openLanguageSettings()
        })
        alert.addAction(UIAlertAction(title: loc("pause_maps"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { self?.showMapPicker() }
        })
        present(alert, animated: true)
    }

    /// Pause menu while walking a Realms map: movement options, other maps and the way back.
    private func showMapPauseMenu() {
        setMenuPaused(true)
        let alert = UIAlertController(title: locF("notice_map_mode", mapName), message: nil, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: loc("pause_resume"), style: .cancel) { [weak self] _ in
            self?.setMenuPaused(false, sound: UiSound.close)
        })
        alert.addAction(UIAlertAction(title: flyingNow ? loc("pause_noclip_off") : loc("pause_noclip_on"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(6)
        })
        alert.addAction(UIAlertAction(title: loc("pause_spawn"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(35)
        })
        if mapGame {
            // Quake 2's quick save / load (F6 / F9 on the desktop)
            alert.addAction(UIAlertAction(title: loc("pause_quick_save"), style: .default) { [weak self] _ in
                self?.setMenuPaused(false)
                self?.bridge?.sendCommand(58)
            })
            alert.addAction(UIAlertAction(title: loc("pause_quick_load"), style: .default) { [weak self] _ in
                self?.setMenuPaused(false)
                self?.bridge?.sendCommand(59)
            })
        }
        alert.addAction(UIAlertAction(title: loc("pause_maps"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { self?.showMapPicker() }
        })
        alert.addAction(UIAlertAction(title: loc("pause_leave_map"), style: .destructive) { [weak self] _ in
            self?.setMenuPaused(false)
            self?.bridge?.sendCommand(50)
        })
        present(alert, animated: true)
    }

    /// Quake 2 BSPs and glTF scenes from the app bundle (maps/, models/) and the
    /// writable support directory (maps/), as paths relative to those roots.
    private func availableMaps() -> [String] {
        let extensions: Set<String> = ["bsp", "glb", "gltf"]
        let fm = FileManager.default
        var found: [String] = []
        func scan(_ root: String, _ folder: String) {
            let names = (try? fm.contentsOfDirectory(atPath: (root as NSString).appendingPathComponent(folder))) ?? []
            for name in names.sorted() where extensions.contains((name as NSString).pathExtension.lowercased()) {
                let path = "\(folder)/\(name)"
                if !found.contains(path) { found.append(path) }
            }
        }
        if let bundle = Bundle.main.resourcePath {
            scan(bundle, "maps")
            scan(bundle, "models")
        }
        for name in importedMaps() where !found.contains("maps/\(name)") {
            found.append("maps/\(name)")
        }
        // The sandbox's own street first.
        let city = found.filter { $0.hasSuffix("abandoned_city_street_for_games.glb") }
        return city + found.filter { !$0.hasSuffix("abandoned_city_street_for_games.glb") }
    }

    private func showMapPicker() {
        guard worldReady, appActive, presentedViewController == nil else { return }
        let maps = availableMaps()
        setMenuPaused(true)
        let alert = UIAlertController(title: loc("maps_title"), message: maps.isEmpty ? loc("maps_empty") : nil, preferredStyle: .alert)
        for path in maps {
            let name = (path as NSString).lastPathComponent
            alert.addAction(UIAlertAction(title: name, style: .default) { [weak self] _ in
                self?.setMenuPaused(false)
                self?.showToast(locF("map_loading", name))
                self?.bridge?.loadMap(path)
            })
        }
        alert.addAction(UIAlertAction(title: loc("maps_import"), style: .default) { [weak self] _ in
            self?.setMenuPaused(false)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { self?.showMapImporter() }
        })
        if !importedMaps().isEmpty {
            alert.addAction(UIAlertAction(title: loc("maps_remove"), style: .destructive) { [weak self] _ in
                self?.setMenuPaused(false)
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { self?.showRemoveMapPicker() }
            })
        }
        alert.addAction(UIAlertAction(title: loc("cancel"), style: .cancel) { [weak self] _ in
            self?.setMenuPaused(false, sound: UiSound.close)
        })
        present(alert, animated: true)
    }

    /// Imported maps live in the engine's writable root (Application Support/EkronSandbox/maps).
    private func importedMapsDirectory() -> URL? {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("EkronSandbox", isDirectory: true)
            .appendingPathComponent("maps", isDirectory: true)
    }

    private func importedMaps() -> [String] {
        guard let dir = importedMapsDirectory() else { return [] }
        return ((try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []).filter(isMapFileName).sorted()
    }

    /// "Import map…": the system file picker; a .gltf may be picked together with its .bin and textures.
    private func showMapImporter() {
        guard worldReady, appActive, presentedViewController == nil else { return }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.data], asCopy: true)
        picker.allowsMultipleSelection = true
        picker.delegate = self
        setMenuPaused(true)
        present(picker, animated: true)
    }

    /// Copies picked files into the imported maps folder off the main thread, then loads the first map among them.
    fileprivate func importMapFiles(_ urls: [URL]) {
        guard let dir = importedMapsDirectory() else { return }
        let limit = SandboxBridge.mapImportLimit()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            var firstMap: String?
            var imported = 0
            var rejected = 0
            for url in urls {
                let name = SandboxBridge.mapImportFileName(url.lastPathComponent)
                if name.isEmpty || !copyPickedMapFile(url, to: dir.appendingPathComponent(name), name: name, limit: limit) {
                    rejected += 1
                    continue
                }
                imported += 1
                if firstMap == nil && isMapFileName(name) { firstMap = name }
            }
            let map = firstMap
            let added = imported
            let failed = rejected
            DispatchQueue.main.async {
                guard let self = self else { return }
                if let map = map, self.worldReady, self.appActive {
                    self.showToast(locF("map_loading", map))
                    self.bridge?.loadMap("maps/\(map)")
                } else if added > 0 && failed == 0 {
                    self.showToast(loc("maps_imported_files"))
                }
                if failed > 0 && self.presentedViewController == nil {
                    let alert = UIAlertController(title: loc("maps_title"), message: loc("maps_import_failed"), preferredStyle: .alert)
                    alert.addAction(UIAlertAction(title: loc("ok"), style: .default) { [weak self] _ in
                        self?.setMenuPaused(false)
                    })
                    self.setMenuPaused(true, sound: UiSound.error)
                    self.present(alert, animated: true)
                }
            }
        }
    }

    /// Imported maps other than the one being walked.
    private func showRemoveMapPicker() {
        guard worldReady, appActive, presentedViewController == nil, let dir = importedMapsDirectory() else { return }
        let current = mapMode ? (bridge?.currentMapPath() ?? "") : ""
        let maps = importedMaps().filter { "maps/\($0)" != current }
        guard !maps.isEmpty else { return }
        setMenuPaused(true)
        let alert = UIAlertController(title: loc("maps_remove_title"), message: nil, preferredStyle: .alert)
        for name in maps {
            alert.addAction(UIAlertAction(title: name, style: .destructive) { [weak self] _ in
                self?.setMenuPaused(false)
                if (try? FileManager.default.removeItem(at: dir.appendingPathComponent(name))) != nil {
                    self?.showToast(locF("maps_removed", name))
                }
            })
        }
        alert.addAction(UIAlertAction(title: loc("cancel"), style: .cancel) { [weak self] _ in
            self?.setMenuPaused(false, sound: UiSound.close)
        })
        present(alert, animated: true)
    }

    /// English, Russian, Portuguese (Portugal) and Spanish (Spain) follow the per-app language in iOS Settings.
    private func openLanguageSettings() {
        if let url = URL(string: UIApplication.openSettingsURLString), UIApplication.shared.canOpenURL(url) {
            UIApplication.shared.open(url)
        } else {
            showToast(loc("language_settings_hint"))
        }
    }

    private func showOnlineDialog() {
        guard worldReady, appActive, presentedViewController == nil else { return }
        setMenuPaused(true)
        let alert = UIAlertController(title: loc("online_title"),
            message: loc("online_disclaimer"),
            preferredStyle: .alert)
        alert.addTextField { $0.placeholder = loc("online_server_hint"); $0.autocapitalizationType = .none; $0.autocorrectionType = .no }
        alert.addTextField { $0.placeholder = loc("online_port_hint"); $0.text = "38475"; $0.keyboardType = .numberPad }
        alert.addTextField { $0.placeholder = loc("online_name_hint"); $0.text = loc("online_default_name") }
        alert.addTextField { $0.placeholder = loc("online_token_hint"); $0.isSecureTextEntry = true; $0.autocapitalizationType = .none }
        alert.addAction(UIAlertAction(title: loc("cancel"), style: .cancel) { [weak self] _ in self?.setMenuPaused(false, sound: UiSound.close) })
        alert.addAction(UIAlertAction(title: loc("online_connect"), style: .default) { [weak self, weak alert] _ in
            guard let self = self, let fields = alert?.textFields, fields.count == 4 else { return }
            self.setMenuPaused(false)
            let host = (fields[0].text ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            let name = (fields[2].text ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            let token = fields[3].text ?? ""
            guard !host.isEmpty, host.utf8.count <= 253,
                  let port = UInt16(fields[1].text ?? ""), port > 0,
                  !name.isEmpty, name.unicodeScalars.count <= 32, (32...256).contains(token.utf8.count) else {
                self.showToast(loc("online_invalid_input"))
                return
            }
            self.online.connect(host: host, port: port, name: name, token: token)
        })
        present(alert, animated: true)
    }

    @objc private func copyCoordinatesToClipboard() {
        let json = String(format: "{\"enabled\":true,\"position\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"yawDegrees\":%.4f,\"startFlying\":%@}",
                          locale: Locale(identifier: "en_US_POSIX"),
                          latestPosition[0], latestPosition[1], latestPosition[2], latestYawDegrees, flyingNow ? "true" : "false")
        UIPasteboard.general.string = json
        showToast(loc("spawn_copied"))
    }

    private func showToast(_ message: String) {
        toastHideWorkItem?.cancel()
        toastLabel.text = "  \(message)  "
        toastLabel.isHidden = false
        hudOverlay.bringSubviewToFront(toastLabel)
        let work = DispatchWorkItem { [weak self] in
            self?.toastLabel.isHidden = true
        }
        toastHideWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.8, execute: work)
    }
}

extension GameViewController: UIDocumentPickerDelegate {
    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        setMenuPaused(false)
        importMapFiles(urls)
    }

    func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
        setMenuPaused(false, sound: UiSound.close)
    }
}

/// .bsp, .glb or .gltf: the files the map list shows and loads.
private func isMapFileName(_ name: String) -> Bool {
    ["bsp", "glb", "gltf"].contains((name as NSString).pathExtension.lowercased())
}

/// Header-checked (shared mapview rules), size-capped copy through a .part file,
/// so a map is never half-written. Runs off the main thread.
private func copyPickedMapFile(_ source: URL, to target: URL, name: String, limit: Int64) -> Bool {
    let fm = FileManager.default
    let part = target.appendingPathExtension("part")
    defer { try? fm.removeItem(at: part) }
    let scoped = source.startAccessingSecurityScopedResource()
    defer { if scoped { source.stopAccessingSecurityScopedResource() } }
    guard let input = try? FileHandle(forReadingFrom: source) else { return false }
    defer { try? input.close() }
    let head = (try? input.read(upToCount: 64)) ?? Data()
    guard !head.isEmpty, SandboxBridge.mapImportHeaderOk(head, fileName: name) else { return false }
    guard fm.createFile(atPath: part.path, contents: nil), let output = try? FileHandle(forWritingTo: part) else { return false }
    defer { try? output.close() }
    var total = Int64(head.count)
    do {
        try output.write(contentsOf: head)
        while let chunk = try input.read(upToCount: 1 << 16), !chunk.isEmpty {
            total += Int64(chunk.count)
            if total > limit { return false }
            try output.write(contentsOf: chunk)
        }
        try output.synchronize()
        if fm.fileExists(atPath: target.path) {
            _ = try fm.replaceItemAt(target, withItemAt: part)
        } else {
            try fm.moveItem(at: part, to: target)
        }
    } catch {
        return false
    }
    return true
}
