package com.example.realms

import android.os.Bundle
import android.graphics.Color
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.google.androidgamesdk.GameActivity

/** Native GameActivity + GLES renderer. There is no browser or WebView here. */
class MainActivity : GameActivity() {
    companion object { init { System.loadLibrary("realms") } }
    private external fun nativeCommand(command: Int)
    private val items = arrayOf("Стена", "Пол", "Окно", "Дверь", "Стол", "Стул", "Шкаф", "Диван")
    private var selected = 0
    private var building = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val overlay = FrameLayout(this)
        val title = TextView(this).apply {
            text = "ЭКРОН • ПЕСОЧНИЦА\nСлева — ходьба · справа — обзор"
            setTextColor(Color.WHITE)
            textSize = 14f
            setPadding(20, 12, 20, 12)
            setBackgroundColor(0x88202a2d.toInt())
        }
        overlay.addView(title, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        overlay.addView(TextView(this).apply {
            text = "+"; textSize = 26f; setTextColor(Color.WHITE)
        }, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setBackgroundColor(0xbb202a2d.toInt())
        }
        fun button(label: String, action: (Button) -> Unit): Button {
            val b = Button(this).apply {
                text = label; textSize = 11f; isAllCaps = false
                minWidth = 0; minimumWidth = 0
                setPadding(12, 0, 12, 0)
                setOnClickListener { action(this) }
            }
            bar.addView(b, LinearLayout.LayoutParams(-2, -2))
            return b
        }
        val catalog = button(items[selected]) { b ->
            selected = (selected + 1) % items.size
            b.text = items[selected]; nativeCommand(1)
        }
        val rotate = button("Повернуть") { nativeCommand(2) }
        val place = button("Поставить") { nativeCommand(3) }
        catalog.visibility = View.GONE; rotate.visibility = View.GONE; place.visibility = View.GONE
        button("Строить") { b ->
            building = !building; b.text = if (building) "Готово" else "Строить"
            val visible = if (building) View.VISIBLE else View.GONE
            catalog.visibility = visible; rotate.visibility = visible; place.visibility = visible
            nativeCommand(0)
        }
        button("Убрать своё") { nativeCommand(4) }
        overlay.addView(bar, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL))
        // Non-clickable overlay background passes movement touches to the native surface.
        addContentView(overlay, ViewGroup.LayoutParams(-1, -1))
        hideSystemUi()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    @Suppress("DEPRECATION")
    private fun hideSystemUi() {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}
