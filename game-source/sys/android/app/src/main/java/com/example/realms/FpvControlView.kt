package com.example.realms

import android.content.Context
import android.graphics.*
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/** Two invisible thumb zones; only the containing exit button is drawn as HUD. */
class FpvControlView(context: Context, private val move: (Float,Float)->Unit,
                     private val lift: (Float)->Unit, private val look: (Float,Float)->Unit) : View(context) {
    private data class Thumb(val x: Float,val y: Float,var lastX: Float,var lastY: Float,val left: Boolean)
    private val thumbs=mutableMapOf<Int,Thumb>()
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG)
    private var vignette: Shader?=null
    init { contentDescription="FPV: слева движение; справа вверх и вниз — высота, в стороны — поворот";isClickable=true }
    fun reset(){thumbs.clear();move(0f,0f);lift(0f)}
    override fun onSizeChanged(w:Int,h:Int,oldw:Int,oldh:Int){
        vignette=RadialGradient(w/2f,h/2f,max(w,h)*.65f,intArrayOf(Color.TRANSPARENT,0x99050b10.toInt()),floatArrayOf(.35f,1f),Shader.TileMode.CLAMP)
    }
    override fun onDraw(canvas:Canvas){paint.shader=vignette;canvas.drawRect(0f,0f,width.toFloat(),height.toFloat(),paint);paint.shader=null}
    override fun onTouchEvent(e:MotionEvent):Boolean {
        when(e.actionMasked){
            MotionEvent.ACTION_DOWN,MotionEvent.ACTION_POINTER_DOWN->{val i=e.actionIndex;val left=e.getX(i)<width/2f
                if(thumbs.values.none { it.left==left })thumbs[e.getPointerId(i)]=Thumb(e.getX(i),e.getY(i),e.getX(i),e.getY(i),left)
                parent.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE->{val range=80*resources.displayMetrics.density
                for(i in 0 until e.pointerCount){val t=thumbs[e.getPointerId(i)]?:continue
                    if(t.left)move(((e.getX(i)-t.x)/range).coerceIn(-1f,1f),((t.y-e.getY(i))/range).coerceIn(-1f,1f))
                    else {lift(((t.y-e.getY(i))/range).coerceIn(-1f,1f));look(e.getX(i)-t.lastX,0f)}
                    t.lastX=e.getX(i);t.lastY=e.getY(i)
                }
            }
            MotionEvent.ACTION_POINTER_UP->{thumbs.remove(e.getPointerId(e.actionIndex))?.let { if(it.left)move(0f,0f) else lift(0f) }}
            MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->reset()
        };return true
    }
}
