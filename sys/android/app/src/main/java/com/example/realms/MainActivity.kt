package com.example.realms

import android.app.AlertDialog
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.util.concurrent.Executors
import android.graphics.drawable.GradientDrawable
import android.media.MediaPlayer
import android.view.Surface
import android.view.TextureView
import android.graphics.SurfaceTexture
import android.os.SystemClock
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.graphics.Color
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.*
import java.util.Locale
import com.google.androidgamesdk.GameActivity

/** Android HUD over the native GLES scene; loading remains visible until its first frame. */
class MainActivity : GameActivity() {
    companion object { init { System.loadLibrary("realms") } }
    private external fun nativeReady(ready: Boolean)
    private var player: MediaPlayer? = null
    private var video: TextureView? = null
    private var videoEndedAt = Long.MAX_VALUE
    private var active = false
    private var introFinished = false
    private external fun nativeCommand(command: Int)
    private external fun nativeStatus(): FloatArray
    private external fun nativeVertical(value: Float)
    private external fun nativeDroneInput(throttle:Float,yaw:Float,pitch:Float,roll:Float)
    private external fun nativeMove(right: Float, forward: Float)
    private external fun nativeLook(dx: Float, dy: Float)
    private external fun nativeRemotePlayers(values: FloatArray)
    private external fun nativeLoadMap(path: String)
    private external fun nativeMapPath(): String
    /** Realms map mode (status slot 17): the sandbox HUD steps aside while a map is walked. */
    private var mapMode = false
    private var mapTexturesLoading = false
    private var mapName = ""
    private lateinit var switchControl: HudButton
    private val online by lazy {
        OnlinePresence({ nativeRemotePlayers(it) }, { message ->
            if (active && !isFinishing) Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        })
    }
    private lateinit var lookArea: LookView
    private lateinit var fpvLayer: FrameLayout
    private lateinit var fpvControls: FpvControlView
    private lateinit var dronePlace: HudButton
    private lateinit var remoteActions: LinearLayout
    private var droneState=0
    private val normalHudVisibility=mutableMapOf<View,Int>()
    private var paused = false
    private var worldReady = false
    private var flyingNow = false
    private lateinit var joystick: StickView
    private val toolsState = ToolState()
    private var requestedTool: HandTool? = null
    private lateinit var toolActions: LinearLayout
    private var interactionCode=0
    private lateinit var interactionControl: HudButton
    private lateinit var catalogControl: HudButton
    private lateinit var spawnActions: LinearLayout
    private lateinit var gravityActions: LinearLayout
    private lateinit var removeControl: HudButton
    private lateinit var freezeControl: HudButton
    private lateinit var grabControl: HudButton
    private lateinit var rotateHeldControl: HudButton
    private lateinit var crouchControl: HudButton
    private lateinit var proneControl: HudButton
    // Localized via res/values{,-ru,-pt-rPT}; resolved lazily because resources are not ready during construction.
    private val items by lazy { BuildCatalog.entries.map { getString(it.labelRes) }.toTypedArray() }
    private val catalogImages=arrayOfNulls<Bitmap>(BuildCatalog.entries.size)
    private val catalogExecutor=Executors.newSingleThreadExecutor()
    private var catalogAdapter: BaseAdapter? = null
    private lateinit var hud: FrameLayout
    private lateinit var toolWheel: ToolWheelView
    private lateinit var weapon: ToolArtView
    private lateinit var selectedImage: ImageView
    private lateinit var selectedSlot: FrameLayout
    private lateinit var selectedSwatch: MaterialBlockView
    private var popup: View? = null
    private var pendingSelection: Int? = null
    private var pendingSurface: Int? = null
    private var selectedSurface=1
    private val materialImages=arrayOfNulls<Bitmap>(7)
    private val materialIds=intArrayOf(5,1,2,3)
    private val materialNames by lazy { arrayOf(R.string.material_wood,R.string.material_concrete,R.string.material_brick,R.string.material_metal).map { getString(it) } }
    private var selected = 0
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var coordinates: TextView
    private lateinit var flight: Button
    private lateinit var altitude: LinearLayout
    private lateinit var loading: LinearLayout
    private lateinit var loadingText: TextView
    private lateinit var spinner: ProgressBar
    private lateinit var notice: TextView
    private var latest = floatArrayOf(0f, 1.7f, 0f)
    private var latestYawDegrees = 0f
    private val poll = object : Runnable {
        override fun run() {
            val state = nativeStatus()
            online.updatePose(state)
            if (state.size >= 6) {
                if(state.size >= 8) {
                    val confirmed=HandTool.values().getOrElse(state[7].toInt()) { HandTool.HAND_EMPTY }
                    if(requestedTool==confirmed)requestedTool=null
                    if(requestedTool==null&&confirmed!=toolsState.tool)toolsState.select(confirmed)
                    if(confirmed==toolsState.tool)toolsState.confirmHolding(state[6]>.5f)
                    if(state.size>=11 && state[4].toInt()==6){
                        val nativeType=state[9].toInt();val nativeSurface=state[10].toInt()
                        val nativeIndex=BuildCatalog.entries.indexOfFirst { it.nativeType==nativeType }
                        if(pendingSelection==nativeIndex)pendingSelection=null
                        if(pendingSurface==nativeSurface)pendingSurface=null
                        if(pendingSelection==null && nativeIndex in items.indices){selected=nativeIndex;selectedSurface=BuildCatalog.entries[selected].defaultSurface}
                        if(pendingSurface==null && nativeSurface in listOf(1,2,3,5,6))selectedSurface=nativeSurface
                    }
                    if(state.size>=12){interactionCode=state[11].toInt();interactionControl.visibility=if(interactionCode>0)View.VISIBLE else View.GONE
                        interactionControl.caption=when(interactionCode){6->getString(R.string.interaction_connect);1->getString(R.string.interaction_open);2->getString(R.string.interaction_close);3->getString(R.string.interaction_sit);4->getString(R.string.interaction_exit);else->""}
                        interactionControl.contentDescription=interactionControl.caption;interactionControl.invalidate()
                    }
                    if(state.size>=13){freezeControl.caption=if(state[12]>.5f) getString(R.string.action_unfreeze) else getString(R.string.action_freeze);freezeControl.contentDescription=freezeControl.caption;freezeControl.invalidate()}
                    if(state.size>=15){crouchControl.isSelected=state[14].toInt()==1;proneControl.isSelected=state[14].toInt()==2}
                    renderTools()
                }
                val nowMap = state.size >= 18 && state[17] > 0.5f
                mapTexturesLoading = state.size >= 18 && state[17] > 1.5f
                // Re-read every poll: picking another map keeps map mode on.
                if (nowMap) nativeMapPath().substringAfterLast('/').takeIf { it.isNotEmpty() }?.let { mapName = it }
                if (nowMap != mapMode) {
                    mapMode = nowMap
                    if (!nowMap) mapName = ""
                    if (nowMap) closePopup()
                    renderTools()
                }
                latest = state.copyOfRange(0, 3)
                if(state.size>=16) latestYawDegrees = Math.toDegrees(state[15].toDouble()).toFloat()
                coordinates.text = String.format(Locale.US, "X: %.1f   Y: %.1f   Z: %.1f", state[0], state[1], state[2])
                val flying = state[3] > 0.5f
                flyingNow = flying
                flight.text = getString(if (flying) R.string.flight_on else R.string.flight_off)
                altitude.visibility = if (flying) View.VISIBLE else View.GONE
                val stage = state[4].toInt()
                worldReady = stage == 6 && introFinished
                val unlocked = worldReady && active
                nativeReady(unlocked && !paused)
                weapon.running=unlocked && !paused
                loading.visibility = if (unlocked) View.GONE else View.VISIBLE
                spinner.visibility = if (stage < 0) View.GONE else View.VISIBLE
                if(state.size>=9)spinner.progress=state[8].toInt().coerceIn(0,100)
                loadingText.text = when (stage) {
                    0 -> getString(R.string.loading_stage_graphics)
                    1 -> getString(R.string.loading_stage_textures)
                    2 -> getString(R.string.loading_stage_city)
                    3 -> getString(R.string.loading_stage_vehicle)
                    4 -> getString(R.string.loading_stage_gpu)
                    5 -> getString(R.string.loading_stage_first_frame)
                    6 -> getString(R.string.loading_stage_ready)
                    else -> getString(R.string.loading_failed)
                }
                if(stage in 0..5 && state.size>=9)loadingText.append(" ${state[8].toInt()}%")
                notice.text = if (mapMode) {
                    if (flying) getString(R.string.notice_flying)
                    else if (mapTexturesLoading) getString(R.string.notice_map_textures, mapName)
                    else getString(R.string.notice_map_mode, mapName)
                } else when (state[5].toInt()) {
                    1 -> getString(R.string.notice_spawn_moved)
                    7 -> getString(R.string.notice_map_failed)
                    2 -> getString(R.string.notice_spawn_unavailable)
                    3 -> getString(R.string.notice_no_parking)
                    5 -> getString(R.string.notice_no_exit)
                    4 -> getString(R.string.notice_player_respawned)
                    else -> if (flying) getString(R.string.notice_flying) else when(toolsState.tool) {
                        HandTool.HAND_EMPTY -> getString(R.string.tool_empty_hands)
                        HandTool.SPAWN_GUN -> getString(R.string.notice_spawn_gun, items[selected])
                        HandTool.GRAVITY_GUN -> if(toolsState.holding) getString(R.string.notice_item_grabbed) else getString(R.string.notice_gravity_gun)
                        HandTool.DELETE_TOOL -> getString(R.string.notice_delete_tool)
                        HandTool.DRONE_REMOTE -> getString(R.string.notice_fpv_remote)
                    }
                }
            }
            if(state.size>=14 && ::fpvLayer.isInitialized){
                droneState=state[13].toInt()
                dronePlace.visibility=if(droneState==1)View.VISIBLE else View.GONE
                val fpv=droneState in 3..5
                if(fpv){
                    if(normalHudVisibility.isEmpty()){
                        closePopup();toolWheel.cancel();joystick.reset();lookArea.reset()
                        for(i in 0 until hud.childCount){val child=hud.getChildAt(i);if(child!==fpvLayer)normalHudVisibility[child]=child.visibility}
                        Toast.makeText(this@MainActivity,getString(R.string.fpv_controls_hint),Toast.LENGTH_LONG).show()
                    }
                    for(i in 0 until hud.childCount){val child=hud.getChildAt(i);if(child!==fpvLayer)child.visibility=View.INVISIBLE}
                    fpvLayer.visibility=View.VISIBLE;fpvLayer.bringToFront()
                }else {
                    fpvLayer.visibility=View.GONE
                    if(normalHudVisibility.isNotEmpty()){
                        fpvControls.reset();normalHudVisibility.forEach { (view,visibility)->view.visibility=visibility };normalHudVisibility.clear();renderTools()
                        dronePlace.visibility=if(droneState==1)View.VISIBLE else View.GONE
                    }
                }
            }
            if (!introFinished && videoEndedAt != Long.MAX_VALUE && SystemClock.elapsedRealtime() - videoEndedAt >= 3000) introFinished = true
            handler.postDelayed(this, 100)
        }
    }
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun label(text: String, size: Float) = TextView(this).apply {
        this.text = text; textSize = size; setTextColor(Color.WHITE)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val overlay = FrameLayout(this).apply { isMotionEventSplittingEnabled = true }
        hud=overlay
        lookArea=LookView(this) { dx,dy -> nativeLook(dx,dy) }
        overlay.addView(lookArea,FrameLayout.LayoutParams(-1,-1))
        weapon=ToolArtView(this,true).apply { visibility=View.GONE }
        overlay.addView(weapon,FrameLayout.LayoutParams(dp(200),dp(125),Gravity.BOTTOM or Gravity.END).apply { rightMargin=dp(125);bottomMargin=dp(78) })
        val heading = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            background = GradientDrawable().apply { setColor(0xaa263039.toInt()); cornerRadius = dp(5).toFloat() }
        }
        heading.addView(label(getString(R.string.hud_title), 14f))
        coordinates = label("X: —   Y: —   Z: —", 13f).apply {
            contentDescription = getString(R.string.coordinates_description)
            setOnClickListener {
                val json = String.format(Locale.US, "{\"enabled\":true,\"position\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"yawDegrees\":%.4f,\"startFlying\":%s}", latest[0], latest[1], latest[2], latestYawDegrees, flyingNow.toString())
                (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("spawn.json", json))
                Toast.makeText(this@MainActivity, R.string.spawn_copied, Toast.LENGTH_SHORT).show()
            }
        }
        heading.addView(coordinates)
        notice = label(getString(R.string.notice_free_mode), 10f)
        heading.addView(notice)
        overlay.addView(heading, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        overlay.addView(label("+", 26f), FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        // The reference HUD scales by available landscape height, not by a fixed phone resolution.
        val screenHeight = resources.configuration.screenHeightDp
        val actionSize = (screenHeight * .145f).toInt().coerceIn(60, 64)
        val stickSize = (screenHeight * .32f).toInt().coerceIn(120, 184)
        joystick = StickView(this) { right, forward -> weapon.moving=kotlin.math.hypot(right,forward)>.1f;nativeMove(right, forward) }
        overlay.addView(joystick, FrameLayout.LayoutParams(dp(stickSize), dp(stickSize), Gravity.BOTTOM or Gravity.START).apply {
            leftMargin = dp(14); bottomMargin = dp(22)
        })
        fun action(caption: String, icon: String, click: () -> Unit): HudButton = HudButton(this, caption, icon).apply {
            setOnClickListener { click() }
        }
        val pause = action("", "pause") { showPauseMenu() }.apply { visualScale=.73f }
        pause.contentDescription = getString(R.string.pause)
        overlay.addView(pause, FrameLayout.LayoutParams(dp(60), dp(60), Gravity.TOP or Gravity.END).apply { topMargin=dp(6); rightMargin=dp(8) })
        val movement = LinearLayout(this).apply { orientation=LinearLayout.VERTICAL; gravity=Gravity.CENTER; isMotionEventSplittingEnabled=true }
        val jump = action(getString(R.string.action_jump), "jump") { nativeCommand(8) }
        // Fire on press, not release. The joystick keeps its own pointer and axes untouched.
        jump.setOnTouchListener { v, event ->
            when(event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { v.isPressed=true;v.performClick() }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> v.isPressed=false
            }
            true
        }
        crouchControl = action(getString(R.string.action_crouch), "crouch") {
            crouchControl.isSelected = !crouchControl.isSelected; proneControl.isSelected=false
            nativeCommand(9)
        }
        proneControl = action(getString(R.string.action_prone), "prone") {
            proneControl.isSelected = !proneControl.isSelected; crouchControl.isSelected=false
            nativeCommand(10)
        }
        for (control in listOf(jump,crouchControl)) movement.addView(control, LinearLayout.LayoutParams(dp(actionSize),dp(actionSize)))
        overlay.addView(movement, FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.END).apply { bottomMargin=dp(8);rightMargin=dp(8) })
        toolWheel=ToolWheelView(this) { selectTool(it) }
        overlay.addView(toolWheel,FrameLayout.LayoutParams(-1,-1))
        // Click action is retained for accessibility; touch uses hold/drag/release.
        val switchWeapon=action("", "switch") {
            if(worldReady && active && !paused && !mapMode) {
                setPaused(true)
                val dialog = AlertDialog.Builder(this)
                    .setTitle(R.string.tool_picker_title)
                    .setItems(arrayOf(getString(R.string.tool_gravity_gun), getString(R.string.tool_fpv_remote), getString(R.string.tool_empty_hands))) { _, index ->
                        setPaused(false)
                        selectTool(ToolWheelSelection.tools[index])
                    }.create()
                dialog.setOnDismissListener { setPaused(false); hideSystemUi() }
                dialog.show()
            }
        }.apply { visualScale=.73f }
        switchWeapon.contentDescription=getString(R.string.tool_switch_description)
        switchControl=switchWeapon
        switchWeapon.setOnTouchListener { view, event ->
            if(!worldReady || !active || paused || mapMode) { toolWheel.cancel(); true }
            else {
                if(event.actionMasked==MotionEvent.ACTION_DOWN) view.parent.requestDisallowInterceptTouchEvent(true)
                toolWheel.gesture(event, toolsState.tool, open = {
                    closePopup();lookArea.reset();toolWheel.bringToFront()
                }, tap = { view.performClick() })
            }
        }
        overlay.addView(switchWeapon,FrameLayout.LayoutParams(dp(60),dp(60),Gravity.TOP or Gravity.END).apply { topMargin=dp(66);rightMargin=dp(8) })
        catalogControl=action("", "cube") { showBuildCatalog() }.apply { contentDescription=getString(R.string.catalog_open_description);visualScale=.73f }
        overlay.addView(catalogControl,FrameLayout.LayoutParams(dp(60),dp(60),Gravity.TOP or Gravity.END).apply { topMargin=dp(66);rightMargin=dp(72) })
        removeControl=action("", "remove") { nativeCommand(4) }.apply { danger=true;visualScale=.73f;contentDescription=getString(R.string.action_remove) }
        overlay.addView(removeControl,FrameLayout.LayoutParams(dp(60),dp(60),Gravity.TOP or Gravity.END).apply { topMargin=dp(126);rightMargin=dp(72) })
        toolActions=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL;gravity=Gravity.END;isMotionEventSplittingEnabled=true }
        spawnActions=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER }
        gravityActions=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER }
        fun addControl(parent: LinearLayout, caption: String, icon: String, click: () -> Unit): HudButton {
            val control=action(caption,icon,click)
            parent.addView(control,LinearLayout.LayoutParams(dp(72),dp(64)).apply { bottomMargin=dp(4) })
            return control
        }
        selectedSlot=FrameLayout(this).apply {
            background=GradientDrawable().apply { setColor(0xaa223b40.toInt());cornerRadius=dp(8).toFloat();setStroke(dp(1),0xff58dbb3.toInt()) }
            setOnClickListener { showBuildCatalog() }
        }
        selectedImage=ImageView(this).apply { scaleType=ImageView.ScaleType.FIT_CENTER }
        selectedSlot.addView(selectedImage,FrameLayout.LayoutParams(-1,-1))
        selectedSwatch=MaterialBlockView(this,selectedSurface)
        selectedSlot.addView(selectedSwatch,FrameLayout.LayoutParams(dp(25),dp(25),Gravity.BOTTOM or Gravity.END))
        // Selection art remains available inside the catalogue, not as a duplicate HUD opener.
        addControl(spawnActions,"","build") { if(requestedTool==null)nativeCommand(200+BuildCatalog.entries[selected].nativeType) }.apply { contentDescription=getString(R.string.action_place_selected) }
        val turns=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL }
        addControl(turns,"","left") { nativeCommand(31) }.apply { contentDescription=getString(R.string.action_turn_left) }
        addControl(turns,"","right") { nativeCommand(2) }.apply { contentDescription=getString(R.string.action_turn_right) }
        spawnActions.addView(turns)
        grabControl=addControl(gravityActions,"","grab") { nativeCommand(32) }.apply { contentDescription=getString(R.string.action_grab) }
        freezeControl=addControl(gravityActions,getString(R.string.action_freeze),"freeze") { nativeCommand(38) }
        rotateHeldControl=addControl(gravityActions,getString(R.string.action_rotate_held),"rotate") { nativeCommand(33) }
        toolActions.addView(spawnActions)
        toolActions.addView(gravityActions)
        remoteActions=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;visibility=View.GONE }
        addControl(remoteActions,getString(R.string.action_drone_home),"home") { nativeCommand(44) }.apply { contentDescription=getString(R.string.action_drone_home_description) }
        addControl(remoteActions,getString(R.string.action_put_away_remote),"remove") { nativeCommand(42) }.apply { contentDescription=getString(R.string.action_put_away_remote_description) }
        toolActions.addView(remoteActions)
        overlay.addView(toolActions,FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply { bottomMargin=dp(8) })
        interactionControl=action(getString(R.string.interaction_open),"grab") { nativeCommand(if(interactionCode==6)43 else 36) }.apply { visibility=View.GONE }
        overlay.addView(interactionControl,FrameLayout.LayoutParams(dp(80),dp(60),Gravity.BOTTOM or Gravity.END).apply { bottomMargin=dp(8);rightMargin=dp(76) })
        renderTools()
        // Flight is now in the pause menu; its altitude controls do not cover the action column.
        flight = Button(this)
        altitude = LinearLayout(this).apply { orientation=LinearLayout.VERTICAL;visibility=View.GONE }
        fun verticalButton(text: String, direction: Float) {
            altitude.addView(Button(this).apply {
                this.text=text;isAllCaps=false;textSize=11f
                setOnTouchListener { v,e ->
                    when(e.actionMasked) {
                        MotionEvent.ACTION_DOWN -> { nativeVertical(direction);v.isPressed=true }
                        MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL -> { nativeVertical(0f);v.isPressed=false }
                    };true
                }
            },LinearLayout.LayoutParams(dp(84),dp(48)))
        }
        verticalButton(getString(R.string.fly_up),1f);verticalButton(getString(R.string.fly_down),-1f)
        overlay.addView(altitude,FrameLayout.LayoutParams(-2,-2,Gravity.CENTER_VERTICAL or Gravity.END).apply { rightMargin=dp(actionSize+20) })

        // Independent Android widgets remain responsive while GLB parsing runs on the native thread.
        loading = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
            setBackgroundColor(Color.rgb(24, 35, 42)); isClickable = true; isFocusable = true
        }
        loading.addView(label(getString(R.string.loading_title), 32f))
        loading.addView(label(getString(R.string.loading_video_hint), 14f).apply { setPadding(0, dp(12), 0, dp(20)) })
        spinner = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max=100;isIndeterminate=false }
        loading.addView(spinner, LinearLayout.LayoutParams(dp(240), dp(16)))
        loadingText = label(getString(R.string.loading_preparing_world), 14f).apply { gravity = Gravity.CENTER; setPadding(dp(20), dp(18), dp(20), 0) }
        loading.addView(loadingText)
        overlay.addView(loading, FrameLayout.LayoutParams(-1, -1))
        nativeReady(false)
        video = TextureView(this).also { texture ->
            texture.isOpaque = true; texture.isClickable = true
            overlay.addView(texture, FrameLayout.LayoutParams(-1, -1))
            texture.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) {
                    if (videoEndedAt != Long.MAX_VALUE) return
                    val surface = Surface(st)
                    try {
                        player = MediaPlayer().apply {
                            assets.openFd("intro.mp4").use { setDataSource(it.fileDescriptor, it.startOffset, it.length) }
                            setSurface(surface)
                            setOnPreparedListener { if (active) it.start() }
                            setOnCompletionListener { finishVideo() }
                            setOnErrorListener { _, _, _ -> finishVideo(); true }
                            prepareAsync()
                        }
                    } catch (_: Exception) { finishVideo() }
                    finally { surface.release() }
                }
                override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) {}
                override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
                override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean {
                    if (videoEndedAt == Long.MAX_VALUE) finishVideo()
                    return true
                }
            }
        }
        dronePlace=action(getString(R.string.drone_place),"build") { nativeCommand(41) }.apply { visibility=View.GONE }
        overlay.addView(dronePlace,FrameLayout.LayoutParams(dp(90),dp(70),Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply { bottomMargin=dp(10) })
        fpvLayer=FrameLayout(this).apply { visibility=View.GONE;isMotionEventSplittingEnabled=true }
        fpvControls=FpvControlView(this,{throttle,yaw,pitch,roll->nativeDroneInput(throttle,yaw,pitch,roll)},{x,y->nativeLook(x,y)})
        fpvLayer.addView(fpvControls,FrameLayout.LayoutParams(-1,-1))
        fpvLayer.addView(action(getString(R.string.fpv_switch_view),"switch") { nativeCommand(45) }.apply { contentDescription=getString(R.string.fpv_switch_view_description) },FrameLayout.LayoutParams(dp(82),dp(64),Gravity.TOP or Gravity.END).apply { topMargin=dp(8);rightMargin=dp(76) })
        fpvLayer.addView(action("","remove") { fpvControls.reset();nativeCommand(42) }.apply { contentDescription=getString(R.string.fpv_exit) },FrameLayout.LayoutParams(dp(60),dp(60),Gravity.TOP or Gravity.END).apply { topMargin=dp(8);rightMargin=dp(8) })
        overlay.addView(fpvLayer,FrameLayout.LayoutParams(-1,-1))
        addContentView(overlay, ViewGroup.LayoutParams(-1, -1))
        hideSystemUi()
        catalogExecutor.execute {
            for(index in BuildCatalog.entries.indices) {
                val bitmap=try { assets.open(BuildCatalog.entries[index].asset).use { BitmapFactory.decodeStream(it) } } catch (_: Exception) { null }
                handler.post { if(!isDestroyed){catalogImages[index]=bitmap;catalogAdapter?.notifyDataSetChanged();updateSelectedIcon()} }
            }
            val paths=mapOf(5 to "wood",1 to "concrete",2 to "brick",3 to "rust",6 to "fabric")
            for((id,name) in paths){
                val bitmap=try{assets.open("textures/$name.png").use { BitmapFactory.decodeStream(it) }}catch(_:Exception){null}
                handler.post { if(!isDestroyed){materialImages[id]=bitmap;catalogAdapter?.notifyDataSetChanged();updateSelectedIcon()} }
            }
        }
    }
    private fun updateSelectedIcon(){
        if(!::selectedImage.isInitialized)return
        val image=catalogImages[selected]
        if(image!=null)selectedImage.setImageBitmap(image) else selectedImage.setImageResource(android.R.drawable.ic_menu_gallery)
        selectedSwatch.materialId=selectedSurface;selectedSwatch.texture=materialImages.getOrNull(selectedSurface);selectedSwatch.invalidate()
        val material=if(selectedSurface<0) getString(R.string.material_model) else if(selectedSurface==6) getString(R.string.material_fabric) else materialNames[materialIds.indexOf(selectedSurface).coerceAtLeast(0)]
        selectedSlot.contentDescription=getString(R.string.selected_slot_description, items[selected], material)
    }
    private fun closePopup(){popup?.let { hud.removeView(it) };popup=null;catalogAdapter=null}
    private fun panel(title: String): LinearLayout {
        closePopup()
        val panel=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL;isClickable=true;isMotionEventSplittingEnabled=true
            background=GradientDrawable().apply { setColor(0xe6202b33.toInt());cornerRadius=dp(12).toFloat();setStroke(dp(1),0x887eaaa6.toInt()) }
            setPadding(dp(8),dp(4),dp(8),dp(6))
        }
        val heading=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER_VERTICAL }
        heading.addView(label(title,13f),LinearLayout.LayoutParams(0,dp(32),1f))
        heading.addView(label("×",24f).apply { gravity=Gravity.CENTER;contentDescription=getString(R.string.catalog_close);setOnClickListener { closePopup() } },LinearLayout.LayoutParams(dp(48),dp(32)))
        panel.addView(heading)
        val height=resources.configuration.screenHeightDp
        val left=(height*.32f).toInt().coerceIn(120,184)+28
        hud.addView(panel,FrameLayout.LayoutParams((hud.width-dp(left+150)).coerceAtLeast(dp(260)),dp((height-144).coerceAtLeast(152)),Gravity.TOP or Gravity.START).apply { leftMargin=dp(left);topMargin=dp(60) })
        popup=panel;return panel
    }
    private fun renderTools() {
        // Seated in a car or walking a Realms map: no sandbox tools.
        val tools=interactionCode!=4&&!mapMode
        weapon.tool=toolsState.tool
        weapon.visibility=if(toolsState.viewmodelVisible&&tools)View.VISIBLE else View.GONE
        updateSelectedIcon()
        catalogControl.visibility=if(tools)View.VISIBLE else View.GONE
        if(::switchControl.isInitialized)switchControl.visibility=if(!mapMode)View.VISIBLE else View.GONE
        spawnActions.visibility=if(toolsState.spawnVisible&&tools)View.VISIBLE else View.GONE
        remoteActions.visibility=if(toolsState.tool==HandTool.DRONE_REMOTE&&tools)View.VISIBLE else View.GONE
        gravityActions.visibility=if(toolsState.gravityVisible&&tools)View.VISIBLE else View.GONE
        removeControl.visibility=if(toolsState.deleteVisible&&tools)View.VISIBLE else View.GONE
        rotateHeldControl.visibility=if(toolsState.rotateHeldVisible&&tools)View.VISIBLE else View.GONE
        val grabCaption=getString(toolsState.grabCaption)
        if(grabControl.contentDescription!=grabCaption) {
            grabControl.symbol=if(toolsState.holding) "release" else "grab"
            grabControl.contentDescription=grabCaption;grabControl.invalidate()
        }
    }
    private fun selectTool(tool: HandTool) {
        closePopup();requestedTool=tool;toolsState.select(tool);nativeCommand(20+tool.ordinal);renderTools()
    }
    private fun showBuildCatalog(category: Int = 0) {
        if(!worldReady || !active || paused || interactionCode==4)return
        val container=panel(getString(R.string.catalog_title))
        val tabs=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL }
        listOf(R.string.catalog_tab_construction, R.string.catalog_tab_furniture, R.string.catalog_tab_weapons, R.string.catalog_tab_fpv).map { getString(it) }.forEachIndexed { index, title ->
            tabs.addView(Button(this).apply { text=title;textSize=10f;isSelected=index==category
                setTextColor(if(index==category)0xff7dffd2.toInt() else Color.WHITE)
                setOnClickListener { showBuildCatalog(index) }
            },LinearLayout.LayoutParams(0,dp(48),1f))
        }
        container.addView(tabs)
        val visibleItems=BuildCatalog.indicesForCategory(category)
        if(category==3){
            val cards=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL }
            fun card(title:String,asset:String,choose:()->Unit){
                val image=ImageView(this).apply { scaleType=ImageView.ScaleType.FIT_CENTER;contentDescription=title }
                val item=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER;isClickable=true;contentDescription=title;setOnClickListener { choose() } }
                item.addView(image,LinearLayout.LayoutParams(-1,0,1f));item.addView(label(title,12f).apply { gravity=Gravity.CENTER },LinearLayout.LayoutParams(-1,dp(36)))
                cards.addView(item,LinearLayout.LayoutParams(0,-1,1f))
                catalogExecutor.execute {
                    val bitmap=try { assets.open(asset).use { BitmapFactory.decodeStream(it) } }catch(_:Exception){null}
                    handler.post { if(!isDestroyed)image.setImageBitmap(bitmap) }
                }
            }
            card(getString(R.string.catalog_card_remote), "catalog/rendered/fpv_remote.webp"){selectTool(HandTool.DRONE_REMOTE)}
            card(getString(R.string.catalog_card_drone), "catalog/rendered/fpv_drone.webp"){nativeCommand(40);closePopup()}
            container.addView(cards,LinearLayout.LayoutParams(-1,0,1f))
            return
        }
        if(visibleItems.isEmpty()){
            container.addView(label(getString(R.string.catalog_empty),14f).apply { gravity=Gravity.CENTER },LinearLayout.LayoutParams(-1,0,1f))
            return
        }
        val grid=GridView(this).apply { numColumns=4;horizontalSpacing=dp(4);verticalSpacing=dp(4);clipToPadding=false;isMotionEventSplittingEnabled=true }
        val adapter=object : BaseAdapter() {
            override fun getCount()=visibleItems.size
            override fun getItem(position: Int)=items[visibleItems[position]]
            override fun getItemId(position: Int)=visibleItems[position].toLong()
            override fun getView(row: Int, convertView: View?, parent: ViewGroup): View = LinearLayout(this@MainActivity).apply {
                val position=visibleItems[row]
                orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER
                contentDescription=items[position];minimumHeight=dp(84)
                layoutParams=AbsListView.LayoutParams(-1,dp(84))
                background=GradientDrawable().apply {setColor(if(position==selected)0xaa477264.toInt() else 0x443b4652);cornerRadius=dp(6).toFloat();if(position==selected)setStroke(dp(1),0xff92d9ac.toInt())}
                val item=BuildCatalog.entries[position]
                val illustration: View=if(item.nativeType==0||item.nativeType==9||item.nativeType==10||item.nativeType==11||item.nativeType==12)MaterialBlockView(this@MainActivity,item.defaultSurface).apply { texture=materialImages[item.defaultSurface] }
                else ImageView(this@MainActivity).apply {
                    scaleType=ImageView.ScaleType.FIT_CENTER
                    val image=catalogImages[position]
                    if(image!=null)setImageBitmap(image) else setImageResource(android.R.drawable.ic_menu_gallery)
                }
                illustration.isClickable=false;illustration.isFocusable=false
                addView(illustration,LinearLayout.LayoutParams(-1,0,1f))
                addView(label(items[position],10f).apply { gravity=Gravity.CENTER;isClickable=false },LinearLayout.LayoutParams(-1,dp(22)))
            }
        }
        catalogAdapter=adapter;grid.adapter=adapter;container.addView(grid,LinearLayout.LayoutParams(-1,0,1f))
        grid.setOnItemClickListener { _,_,row,_ ->
            val index=visibleItems[row]
            requestedTool=HandTool.SPAWN_GUN;toolsState.select(HandTool.SPAWN_GUN)
            nativeCommand(300+BuildCatalog.entries[index].nativeType)
            pendingSelection=index;selected=index;selectedSurface=BuildCatalog.entries[index].defaultSurface
            updateSelectedIcon();closePopup();renderTools()
        }
    }
    private fun setPaused(value: Boolean) {
        if(::toolWheel.isInitialized)toolWheel.cancel()
        closePopup();paused=value;joystick.reset();lookArea.reset();weapon.running=!value && active;nativeVertical(0f)
        nativeReady(worldReady && active && !paused)
    }
    private fun showPauseMenu() {
        if(mapMode) { showMapPauseMenu(); return }
        setPaused(true)
        val dialog=AlertDialog.Builder(this).setTitle(R.string.pause)
            .setItems(arrayOf(R.string.pause_resume,if(flyingNow) R.string.pause_flight_off else R.string.pause_flight_on,R.string.pause_road,R.string.pause_restore_uaz,
                R.string.pause_spawn,R.string.pause_online,R.string.pause_disconnect,R.string.pause_language,R.string.pause_maps).map { getString(it) }.toTypedArray()) { _, which ->
                setPaused(false)
                if(which==1)nativeCommand(6)
                if(which==2)nativeCommand(7)
                if(which==3)nativeCommand(34)
                if(which==4)nativeCommand(35)
                if(which==5)handler.post { showOnlineDialog() }
                if(which==6)online.disconnect()
                if(which==7)handler.post { openLanguageSettings() }
                if(which==8)handler.post { showMapPicker() }
            }.create()
        dialog.setOnDismissListener { setPaused(false);hideSystemUi() }
        dialog.show()
    }
    /** Pause menu while walking a Realms map: movement options, other maps and the way back. */
    private fun showMapPauseMenu() {
        setPaused(true)
        val dialog=AlertDialog.Builder(this).setTitle(getString(R.string.notice_map_mode, mapName))
            .setItems(arrayOf(R.string.pause_resume,if(flyingNow) R.string.pause_noclip_off else R.string.pause_noclip_on,R.string.pause_spawn,
                R.string.pause_maps,R.string.pause_leave_map).map { getString(it) }.toTypedArray()) { _, which ->
                setPaused(false)
                if(which==1)nativeCommand(6)
                if(which==2)nativeCommand(35)
                if(which==3)handler.post { showMapPicker() }
                if(which==4)nativeCommand(50)
            }.create()
        dialog.setOnDismissListener { setPaused(false);hideSystemUi() }
        dialog.show()
    }
    /** Quake 2 BSPs and glTF scenes from the APK (maps/, models/) and internal storage (maps/). */
    private fun availableMaps(): List<String> {
        val extensions=listOf(".bsp",".glb",".gltf")
        fun mapFile(name: String)=extensions.any { name.lowercase(Locale.ROOT).endsWith(it) }
        val found=linkedSetOf<String>()
        for(dir in listOf("maps","models"))
            try { assets.list(dir)?.sorted()?.filter(::mapFile)?.forEach { found.add("$dir/$it") } } catch(_: Exception) {}
        java.io.File(filesDir,"maps").list()?.sorted()?.filter(::mapFile)?.forEach { found.add("maps/$it") }
        // The sandbox's own street first.
        return found.sortedBy { if(it.endsWith("abandoned_city_street_for_games.glb")) 0 else 1 }
    }
    private fun showMapPicker() {
        if (!worldReady || !active) return
        val maps=availableMaps()
        setPaused(true)
        val builder=AlertDialog.Builder(this).setTitle(R.string.maps_title).setNegativeButton(R.string.cancel, null)
        if(maps.isEmpty()) builder.setMessage(R.string.maps_empty)
        else builder.setItems(maps.map { it.substringAfterLast('/') }.toTypedArray()) { _, which ->
            setPaused(false)
            Toast.makeText(this, getString(R.string.map_loading, maps[which].substringAfterLast('/')), Toast.LENGTH_SHORT).show()
            nativeLoadMap(maps[which])
        }
        val dialog=builder.create()
        dialog.setOnDismissListener { setPaused(false);hideSystemUi() }
        dialog.show()
    }
    /** English, Russian and Portuguese (Portugal) follow the system or per-app language (res/xml/locales_config.xml). */
    private fun openLanguageSettings() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try {
                startActivity(Intent(Settings.ACTION_APP_LOCALE_SETTINGS, Uri.fromParts("package", packageName, null)))
                return
            } catch (_: Exception) {}
        }
        Toast.makeText(this, R.string.language_settings_hint, Toast.LENGTH_LONG).show()
    }
    private fun showOnlineDialog() {
        if (!worldReady || !active) return
        setPaused(true)
        fun field(hintText: String, initial: String = ""): EditText = EditText(this).apply {
            hint = hintText; setText(initial); setSingleLine(true)
        }
        val host = field(getString(R.string.online_server_hint))
        val port = field(getString(R.string.online_port_hint), "38475").apply { inputType = android.text.InputType.TYPE_CLASS_NUMBER }
        val name = field(getString(R.string.online_name_hint), getString(R.string.online_default_name))
        val token = field(getString(R.string.online_token_hint)).apply {
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(8), dp(20), 0)
            addView(TextView(this@MainActivity).apply {
                text = getString(R.string.online_disclaimer)
            })
            addView(host); addView(port); addView(name); addView(token)
        }
        val dialog = AlertDialog.Builder(this).setTitle(R.string.online_title)
            .setView(ScrollView(this).apply { addView(content) })
            .setPositiveButton(R.string.online_connect, null)
            .setNegativeButton(R.string.cancel, null).create()
        dialog.setOnDismissListener { setPaused(false); hideSystemUi() }
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val address = host.text.toString().trim()
                val number = port.text.toString().toIntOrNull()
                val playerName = name.text.toString().trim()
                val key = token.text.toString()
                if (address.isEmpty() || address.length > 253 || number == null || number !in 1..65535 ||
                    playerName.isEmpty() || playerName.length > 32 || key.toByteArray(Charsets.UTF_8).size !in 32..256) {
                    Toast.makeText(this, R.string.online_invalid_input, Toast.LENGTH_LONG).show()
                } else {
                    setPaused(false)
                    online.connect(address, number, playerName, key)
                    dialog.dismiss()
                }
            }
        }
        dialog.show()
    }

    override fun onResume() {
        super.onResume()
        active = true
        try { player?.start() } catch (_: IllegalStateException) {}
        if (::coordinates.isInitialized) { handler.removeCallbacks(poll); handler.post(poll) }
    }
    override fun onPause() {
        online.disconnect()
        if(::toolWheel.isInitialized)toolWheel.cancel()
        if(::fpvControls.isInitialized)fpvControls.reset()
        closePopup();weapon.running=false
        active = false; nativeReady(false)
        if (::joystick.isInitialized) joystick.reset();if (::lookArea.isInitialized)lookArea.reset()
        try { player?.pause() } catch (_: IllegalStateException) {}
        handler.removeCallbacks(poll); nativeVertical(0f)
        super.onPause()
    }
    private fun finishVideo() {
        if (videoEndedAt != Long.MAX_VALUE) return
        videoEndedAt = SystemClock.elapsedRealtime()
        player?.release(); player = null; video?.visibility = View.GONE
    }
    override fun onDestroy() { catalogExecutor.shutdownNow();nativeReady(false); player?.release(); player = null; handler.removeCallbacks(poll); super.onDestroy() }
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi() else { nativeVertical(0f); if (::joystick.isInitialized) joystick.reset();if (::lookArea.isInitialized)lookArea.reset() }
    }
    @Suppress("DEPRECATION")
    private fun hideSystemUi() {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}
