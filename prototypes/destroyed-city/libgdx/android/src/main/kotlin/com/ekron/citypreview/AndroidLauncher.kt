package com.ekron.citypreview

import android.os.Bundle
import android.graphics.*
import android.view.*
import android.widget.*
import com.badlogic.gdx.backends.android.AndroidApplication
import com.badlogic.gdx.backends.android.AndroidApplicationConfiguration
import com.example.city.CityPreview
import com.example.city.FlightInput
import kotlin.math.*

class AndroidLauncher:AndroidApplication(){
    private val flight=FlightInput()
    private fun dp(value:Int)=(resources.displayMetrics.density*value).toInt()
    override fun onCreate(state:Bundle?){
        super.onCreate(state)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val root=FrameLayout(this).apply { isMotionEventSplittingEnabled=true }
        val loading=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER;setBackgroundColor(Color.rgb(24,32,40));isClickable=true
            addView(ProgressBar(this@AndroidLauncher))
            addView(TextView(this@AndroidLauncher).apply {text="Генерация города 12 × 12…\nИсходный LibGDX-код. Первый запуск может занять время.";textSize=16f;gravity=Gravity.CENTER;setTextColor(Color.WHITE)})
        }
        val game=CityPreview(flight){runOnUiThread { loading.visibility=View.GONE }}
        val config=AndroidApplicationConfiguration().apply {useImmersiveMode=true;useAccelerometer=false;useCompass=false;disableAudio=true}
        root.addView(initializeForView(game,config),FrameLayout.LayoutParams(-1,-1))
        root.addView(TextView(this).apply {
            text="ГОРОД · LIBGDX · СВОБОДНЫЙ ПОЛЁТ\nСлева — движение, справа — обзор, ↑/↓ — высота"
            textSize=13f;setTextColor(Color.WHITE);setBackgroundColor(0x99202a32.toInt());setPadding(dp(8),dp(5),dp(8),dp(5))
        },FrameLayout.LayoutParams(-2,-2,Gravity.TOP or Gravity.START).apply {leftMargin=dp(10);topMargin=dp(8)})
        root.addView(DragControl(false),FrameLayout.LayoutParams(dp(160),dp(160),Gravity.BOTTOM or Gravity.START).apply {leftMargin=dp(16);bottomMargin=dp(16)})
        root.addView(DragControl(true),FrameLayout.LayoutParams(dp(160),dp(160),Gravity.BOTTOM or Gravity.END).apply {rightMargin=dp(16);bottomMargin=dp(16)})
        val vertical=LinearLayout(this).apply {orientation=LinearLayout.HORIZONTAL;isMotionEventSplittingEnabled=true}
        fun liftButton(text:String,value:Float){
            vertical.addView(Button(this).apply {
                this.text=text;contentDescription=if(value>0)"Лететь вверх" else "Лететь вниз"
                setOnTouchListener {_,e->when(e.actionMasked){MotionEvent.ACTION_DOWN->flight.lift(value);MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->flight.lift(0f)};true}
            },LinearLayout.LayoutParams(dp(76),dp(60)))
        }
        liftButton("↑",1f);liftButton("↓",-1f)
        root.addView(vertical,FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply {bottomMargin=dp(20)})
        root.addView(Button(this).apply {text="На старт";setOnClickListener {flight.clear();flight.home()}},FrameLayout.LayoutParams(dp(110),dp(56),Gravity.TOP or Gravity.END).apply {rightMargin=dp(8);topMargin=dp(8)})
        root.addView(loading,FrameLayout.LayoutParams(-1,-1));setContentView(root)
    }
    override fun onPause(){flight.clear();super.onPause()}

    private inner class DragControl(private val look:Boolean):View(this@AndroidLauncher){
        private val paint=Paint(Paint.ANTI_ALIAS_FLAG)
        private var pointer=-1
        private var startX=0f;private var startY=0f
        private var knobX=0f;private var knobY=0f
        init {isClickable=true;contentDescription=if(look)"Обзор: ведите пальцем" else "Движение: ведите пальцем"}
        private fun reset(){pointer=-1;knobX=0f;knobY=0f;if(!look)flight.move(0f,0f);invalidate()}
        override fun onTouchEvent(e:MotionEvent):Boolean{
            when(e.actionMasked){
                MotionEvent.ACTION_DOWN->{pointer=e.getPointerId(0);startX=e.x;startY=e.y;parent.requestDisallowInterceptTouchEvent(true)}
                MotionEvent.ACTION_MOVE->{val i=e.findPointerIndex(pointer);if(i<0){reset();return true}
                    val x=e.getX(i);val y=e.getY(i)
                    if(look){flight.look(x-startX,y-startY);startX=x;startY=y}
                    else {var dx=(x-startX)/dp(56);var dy=(y-startY)/dp(56);val length=hypot(dx,dy);if(length>1){dx/=length;dy/=length};knobX=dx;knobY=dy;flight.move(if(abs(dx)<.08f)0f else dx,if(abs(dy)<.08f)0f else -dy)}
                    invalidate()
                }
                MotionEvent.ACTION_POINTER_UP->if(e.getPointerId(e.actionIndex)==pointer)reset()
                MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->reset()
            };return true
        }
        override fun onDraw(c:Canvas){
            val radius=width*.46f;val x=width/2f;val y=height/2f
            paint.style=Paint.Style.FILL;paint.color=0x77253038;c.drawCircle(x,y,radius,paint)
            paint.style=Paint.Style.STROKE;paint.strokeWidth=dp(2).toFloat();paint.color=0xaaddffff.toInt();c.drawCircle(x,y,radius,paint)
            paint.style=Paint.Style.FILL;paint.color=0x99ffffff.toInt();c.drawCircle(x+knobX*radius*.5f,y+knobY*radius*.5f,radius*.27f,paint)
            paint.color=Color.WHITE;paint.textSize=dp(12).toFloat();paint.textAlign=Paint.Align.CENTER;c.drawText(if(look)"ОБЗОР" else "ПОЛЁТ",x,y+radius*.8f,paint)
        }
    }
}
