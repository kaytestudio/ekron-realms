package com.example.realms

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
    private val items = arrayOf("Стена", "Пол", "Окно", "Дверь", "Стол", "Стул", "Шкаф", "Диван")
    private var selected = 0
    private var building = false
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
                latest = state.copyOfRange(0, 3)
                coordinates.text = String.format(Locale.US, "X: %.1f   Y: %.1f   Z: %.1f", state[0], state[1], state[2])
                val flying = state[3] > 0.5f
                flight.text = if (flying) "✈ Полёт: ВКЛ" else "✈ Полёт: ВЫКЛ"
                altitude.visibility = if (flying) View.VISIBLE else View.GONE
                val stage = state[4].toInt()
                val unlocked = stage == 6 && introFinished && active
                nativeReady(unlocked)
                loading.visibility = if (unlocked) View.GONE else View.VISIBLE
                spinner.visibility = if (stage < 0) View.GONE else View.VISIBLE
                loadingText.text = when (stage) {
                    0 -> "Подготовка графики…"
                    1 -> "Загрузка текстур…"
                    2 -> "Загрузка города и материалов…"
                    3 -> "Размещение разбитой машины…"
                    4 -> "Проверка коллизий и безопасного спавна…"
                    5 -> "Подготовка первого кадра…"
                    6 -> "Мир готов"
                    else -> "Не удалось запустить рендерер.\nПерезапусти игру. Ошибка записана в logcat / Ekron."
                }
                notice.text = when (state[5].toInt()) {
                    1 -> "Точка из конфига небезопасна — спавн перенесён на открытое место."
                    2 -> "Нет безопасной точки ходьбы — оставлен полёт."
                    else -> if (flying) "Сквозь стены · ↑ / ↓ — высота" else "Слева — ходьба · справа — обзор"
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
        val overlay = FrameLayout(this)
        val heading = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            setBackgroundColor(0xbb202a2d.toInt())
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
        notice = label("Слева — ходьба · справа — обзор", 10f)
        heading.addView(notice)
        overlay.addView(heading, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        overlay.addView(label("+", 26f), FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        flight = Button(this).apply {
            text = "✈ Полёт: ВЫКЛ"; textSize = 12f; isAllCaps = false
            setOnClickListener { nativeVertical(0f); nativeCommand(6) }
        }
        val debugButtons = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(flight)
            addView(Button(this@MainActivity).apply {
                text = "На дорогу"; textSize = 11f; isAllCaps = false
                setOnClickListener { nativeVertical(0f); nativeCommand(7) }
            })
        }
        overlay.addView(debugButtons, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END).apply {
            topMargin = dp(8); rightMargin = dp(8)
        })
        altitude = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
        fun verticalButton(text: String, direction: Float) {
            val b = Button(this).apply {
                this.text = text; isAllCaps = false; contentDescription = text
                setOnTouchListener { v, event ->
                    when (event.actionMasked) {
                        MotionEvent.ACTION_DOWN -> { nativeVertical(direction); v.isPressed = true }
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                            nativeVertical(0f); v.isPressed = false
                            if (event.actionMasked == MotionEvent.ACTION_UP) v.performClick()
                        }
                    }
                    true
                }
            }
            altitude.addView(b, LinearLayout.LayoutParams(dp(86), dp(56)))
        }
        verticalButton("↑ Вверх", 1f); verticalButton("↓ Вниз", -1f)
        overlay.addView(altitude, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER_VERTICAL or Gravity.END).apply { rightMargin = dp(12) })
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER
            setBackgroundColor(0xbb202a2d.toInt())
        }
        fun button(label: String, action: (Button) -> Unit): Button {
            val b = Button(this).apply {
                text = label; textSize = 11f; isAllCaps = false
                minWidth = 0; minimumWidth = 0; setPadding(dp(8), 0, dp(8), 0)
                setOnClickListener { action(this) }
            }
            bar.addView(b, LinearLayout.LayoutParams(-2, -2)); return b
        }
        val catalog = button(items[selected]) { b -> selected = (selected + 1) % items.size; b.text = items[selected]; nativeCommand(1) }
        val rotate = button("Повернуть") { nativeCommand(2) }
        val place = button("Поставить") { nativeCommand(3) }
        catalog.visibility = View.GONE; rotate.visibility = View.GONE; place.visibility = View.GONE
        button("Строить") { b ->
            building = !building; b.text = if (building) "Готово" else "Строить"
            val visible = if (building) View.VISIBLE else View.GONE
            catalog.visibility = visible; rotate.visibility = visible; place.visibility = visible
            nativeCommand(0)
        }
        button("Прыжок") { nativeCommand(8) }
        button("Присесть") { b -> b.isSelected = !b.isSelected; b.text = if (b.isSelected) "Встать" else "Присесть"; nativeCommand(9) }
        button("Убрать своё") { nativeCommand(4) }
        val scroll = HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(bar) }
        overlay.addView(scroll, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL))

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
    override fun onResume() {
        super.onResume()
        active = true
        try { player?.start() } catch (_: IllegalStateException) {}
        if (::coordinates.isInitialized) { handler.removeCallbacks(poll); handler.post(poll) }
    }
    override fun onPause() {
        active = false; nativeReady(false)
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
        if (hasFocus) hideSystemUi() else nativeVertical(0f)
    }
    @Suppress("DEPRECATION")
    private fun hideSystemUi() {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}
