package com.example.realms

import android.app.AlertDialog
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
    private external fun nativeMove(right: Float, forward: Float)
    private external fun nativeLook(dx: Float, dy: Float)
    private lateinit var lookArea: LookView
    private var paused = false
    private var worldReady = false
    private var flyingNow = false
    private lateinit var joystick: StickView
    private val toolsState = ToolState()
    private var requestedTool: HandTool? = null
    private lateinit var toolActions: LinearLayout
    private lateinit var spawnActions: LinearLayout
    private lateinit var gravityActions: LinearLayout
    private lateinit var removeControl: HudButton
    private lateinit var grabControl: HudButton
    private lateinit var rotateHeldControl: HudButton
    private lateinit var crouchControl: HudButton
    private lateinit var proneControl: HudButton
    private val items = arrayOf("Стена", "Пол", "Окно", "Дверь", "Стол", "Стул", "Шкаф", "Диван")
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
    private val poll = object : Runnable {
        override fun run() {
            val state = nativeStatus()
            if (state.size >= 6) {
                if(state.size >= 8) {
                    val confirmed=HandTool.values().getOrElse(state[7].toInt()) { HandTool.HAND_EMPTY }
                    if(requestedTool==confirmed)requestedTool=null
                    if(requestedTool==null&&confirmed!=toolsState.tool)toolsState.select(confirmed)
                    if(confirmed==toolsState.tool)toolsState.confirmHolding(state[6]>.5f)
                    renderTools()
                }
                latest = state.copyOfRange(0, 3)
                coordinates.text = String.format(Locale.US, "X: %.1f   Y: %.1f   Z: %.1f", state[0], state[1], state[2])
                val flying = state[3] > 0.5f
                flyingNow = flying
                flight.text = if (flying) "✈ Полёт: ВКЛ" else "✈ Полёт: ВЫКЛ"
                altitude.visibility = if (flying) View.VISIBLE else View.GONE
                val stage = state[4].toInt()
                worldReady = stage == 6 && introFinished
                val unlocked = worldReady && active
                nativeReady(unlocked && !paused)
                loading.visibility = if (unlocked) View.GONE else View.VISIBLE
                spinner.visibility = if (stage < 0) View.GONE else View.VISIBLE
                loadingText.text = when (stage) {
                    0 -> "Подготовка графики…"
                    1 -> "Загрузка текстур…"
                    2 -> "Загрузка города и материалов…"
                    3 -> "Размещение УАЗа…"
                    4 -> "Проверка коллизий и безопасного спавна…"
                    5 -> "Подготовка первого кадра…"
                    6 -> "Мир готов"
                    else -> "Не удалось запустить рендерер.\nПерезапусти игру. Ошибка записана в logcat / Ekron."
                }
                notice.text = when (state[5].toInt()) {
                    1 -> "Точка из конфига небезопасна — спавн перенесён на открытое место."
                    2 -> "Нет безопасной точки ходьбы — оставлен полёт."
                    else -> if (flying) "Сквозь стены · ↑ / ↓ — высота" else when(toolsState.tool) {
                        HandTool.HAND_EMPTY -> "Пустые руки"
                        HandTool.SPAWN_GUN -> "Спавн-пушка: ${items[selected]}"
                        HandTool.GRAVITY_GUN -> if(toolsState.holding) "Предмет захвачен" else "Грави-пушка: наведите на свою постройку"
                        HandTool.DELETE_TOOL -> "Удаление своих построек"
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
        lookArea=LookView(this) { dx,dy -> nativeLook(dx,dy) }
        overlay.addView(lookArea,FrameLayout.LayoutParams(-1,-1))
        val heading = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            background = GradientDrawable().apply { setColor(0xaa263039.toInt()); cornerRadius = dp(5).toFloat() }
        }
        heading.addView(label("ЭКРОН • ПЕСОЧНИЦА", 14f))
        coordinates = label("X: —   Y: —   Z: —", 13f).apply {
            contentDescription = "Координаты игрока. Нажмите, чтобы скопировать для конфига"
            setOnClickListener {
                val json = String.format(Locale.US, "{\"enabled\":true,\"position\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"yawDegrees\":0,\"startFlying\":%s}", latest[0], latest[1], latest[2], latest[1].let { kotlin.math.abs(it - 1.7f) > .01f }.toString())
                (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("spawn.json", json))
                Toast.makeText(this@MainActivity, "Координаты для spawn.json скопированы", Toast.LENGTH_SHORT).show()
            }
        }
        heading.addView(coordinates)
        notice = label("Свободный режим", 10f)
        heading.addView(notice)
        overlay.addView(heading, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        overlay.addView(label("+", 26f), FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        // The reference HUD scales by available landscape height, not by a fixed phone resolution.
        val screenHeight = resources.configuration.screenHeightDp
        val actionSize = (screenHeight * .145f).toInt().coerceIn(44, 88)
        val stickSize = (screenHeight * .32f).toInt().coerceIn(120, 184)
        joystick = StickView(this) { right, forward -> nativeMove(right, forward) }
        overlay.addView(joystick, FrameLayout.LayoutParams(dp(stickSize), dp(stickSize), Gravity.BOTTOM or Gravity.START).apply {
            leftMargin = dp(14); bottomMargin = dp(22)
        })
        fun action(caption: String, icon: String, click: () -> Unit): HudButton = HudButton(this, caption, icon).apply {
            setOnClickListener { click() }
        }
        val pause = action("", "pause") { showPauseMenu() }
        pause.contentDescription = "Пауза"
        overlay.addView(pause, FrameLayout.LayoutParams(dp(48), dp(48), Gravity.TOP or Gravity.END).apply { topMargin=dp(6); rightMargin=dp(8) })
        val movement = LinearLayout(this).apply { orientation=LinearLayout.VERTICAL; gravity=Gravity.CENTER; isMotionEventSplittingEnabled=true }
        val jump = action("Прыжок", "jump") { nativeCommand(8) }
        // Fire on press, not release. The joystick keeps its own pointer and axes untouched.
        jump.setOnTouchListener { v, event ->
            when(event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { v.isPressed=true;v.performClick() }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> v.isPressed=false
            }
            true
        }
        crouchControl = action("Присесть", "crouch") {
            crouchControl.isSelected = !crouchControl.isSelected; proneControl.isSelected=false
            nativeCommand(9)
        }
        proneControl = action("Лечь", "prone") {
            proneControl.isSelected = !proneControl.isSelected; crouchControl.isSelected=false
            nativeCommand(10)
        }
        for (control in listOf(jump,crouchControl,proneControl)) movement.addView(control, LinearLayout.LayoutParams(dp(actionSize),dp(actionSize)))
        overlay.addView(movement, FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.END).apply { bottomMargin=dp(6);rightMargin=dp(82) })
        // Only the backpack occupies the bottom-right corner with empty hands.
        val backpack = action("Рюкзак", "backpack") { showToolsMenu() }
        overlay.addView(backpack,FrameLayout.LayoutParams(dp(64),dp(64),Gravity.BOTTOM or Gravity.END).apply { bottomMargin=dp(8);rightMargin=dp(8) })
        toolActions=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL;gravity=Gravity.END;isMotionEventSplittingEnabled=true }
        spawnActions=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER }
        gravityActions=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER }
        fun addControl(parent: LinearLayout, caption: String, icon: String, click: () -> Unit): HudButton {
            val control=action(caption,icon,click)
            parent.addView(control,LinearLayout.LayoutParams(dp(72),dp(64)).apply { bottomMargin=dp(4) })
            return control
        }
        addControl(spawnActions,"Блоки","cube") { showBuildCatalog() }
        addControl(spawnActions,"Поставить блок","cube") { nativeCommand(3) }
        val turns=LinearLayout(this).apply { orientation=LinearLayout.HORIZONTAL }
        addControl(turns,"Влево","left") { nativeCommand(31) }
        addControl(turns,"Вправо","right") { nativeCommand(2) }
        spawnActions.addView(turns)
        grabControl=addControl(gravityActions,"Захват","grab") { nativeCommand(32) }
        rotateHeldControl=addControl(gravityActions,"Вращать","rotate") { nativeCommand(33) }
        removeControl=action("Удалить","remove") { nativeCommand(4) }.apply { danger=true }
        toolActions.addView(spawnActions)
        toolActions.addView(gravityActions)
        toolActions.addView(removeControl,LinearLayout.LayoutParams(dp(72),dp(72)))
        overlay.addView(toolActions,FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply { bottomMargin=dp(8) })
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
        verticalButton("↑ Вверх",1f);verticalButton("↓ Вниз",-1f)
        overlay.addView(altitude,FrameLayout.LayoutParams(-2,-2,Gravity.CENTER_VERTICAL or Gravity.END).apply { rightMargin=dp(actionSize+20) })

        // Independent Android widgets remain responsive while GLB parsing runs on the native thread.
        loading = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
            setBackgroundColor(Color.rgb(24, 35, 42)); isClickable = true; isFocusable = true
        }
        loading.addView(label("ЭКРОН", 32f))
        loading.addView(label("Пока ждёте загрузку мира, вам будет показан короткий видео ролик", 14f).apply { setPadding(0, dp(12), 0, dp(20)) })
        spinner = ProgressBar(this)
        loading.addView(spinner, LinearLayout.LayoutParams(dp(48), dp(48)))
        loadingText = label("Подготовка мира…", 14f).apply { gravity = Gravity.CENTER; setPadding(dp(20), dp(18), dp(20), 0) }
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
        addContentView(overlay, ViewGroup.LayoutParams(-1, -1))
        hideSystemUi()
    }
    private fun renderTools() {
        spawnActions.visibility=if(toolsState.spawnVisible)View.VISIBLE else View.GONE
        gravityActions.visibility=if(toolsState.gravityVisible)View.VISIBLE else View.GONE
        removeControl.visibility=if(toolsState.deleteVisible)View.VISIBLE else View.GONE
        rotateHeldControl.visibility=if(toolsState.rotateHeldVisible)View.VISIBLE else View.GONE
        if(grabControl.caption!=toolsState.grabCaption) {
            grabControl.caption=toolsState.grabCaption
            grabControl.symbol=if(toolsState.holding) "release" else "grab"
            grabControl.contentDescription=toolsState.grabCaption;grabControl.invalidate()
        }
    }
    private fun selectTool(tool: HandTool) {
        requestedTool=tool;toolsState.select(tool);nativeCommand(20+tool.ordinal);renderTools()
        if(tool==HandTool.SPAWN_GUN)showBuildCatalog()
    }
    private fun showToolsMenu() {
        joystick.reset()
        AlertDialog.Builder(this).setTitle("Инструмент в руках")
            .setItems(arrayOf("Пустые руки","Спавн-пушка","Грави-пушка","Удалить")) { _,index -> selectTool(HandTool.values()[index]) }
            .setNegativeButton("Отмена",null).show()
    }
    private fun showBuildCatalog() {
        joystick.reset()
        AlertDialog.Builder(this).setTitle("Выберите блок для установки")
            .setItems(items) { _, index ->
                repeat((index-selected+items.size)%items.size) { nativeCommand(1) }
                selected=index
            }.setNegativeButton("Отмена",null).show()
    }
    private fun setPaused(value: Boolean) {
        paused=value;joystick.reset();lookArea.reset();nativeVertical(0f)
        nativeReady(worldReady && active && !paused)
    }
    private fun showPauseMenu() {
        setPaused(true)
        val dialog=AlertDialog.Builder(this).setTitle("Пауза")
            .setItems(arrayOf("Продолжить",if(flyingNow) "Выключить полёт" else "Включить полёт","На дорогу")) { _, which ->
                setPaused(false)
                if(which==1)nativeCommand(6)
                if(which==2)nativeCommand(7)
            }.create()
        dialog.setOnDismissListener { setPaused(false);hideSystemUi() }
        dialog.show()
    }
    override fun onResume() {
        super.onResume()
        active = true
        try { player?.start() } catch (_: IllegalStateException) {}
        if (::coordinates.isInitialized) { handler.removeCallbacks(poll); handler.post(poll) }
    }
    override fun onPause() {
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
    override fun onDestroy() { nativeReady(false); player?.release(); player = null; handler.removeCallbacks(poll); super.onDestroy() }
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
