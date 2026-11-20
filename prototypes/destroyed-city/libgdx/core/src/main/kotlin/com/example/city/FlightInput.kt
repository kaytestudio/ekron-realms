package com.example.city

/** Only control data crosses the UI/GL thread boundary. */
class FlightInput {
    data class Frame(val right: Float, val forward: Float, val up: Float, val dx: Float, val dy: Float, val reset: Boolean)
    private var right=0f; private var forward=0f; private var up=0f
    private var dx=0f; private var dy=0f; private var reset=false
    private fun clean(value: Float)=if(value.isFinite())value.coerceIn(-1f,1f)else 0f
    @Synchronized fun move(x:Float,z:Float){right=clean(x);forward=clean(z)}
    @Synchronized fun lift(y:Float){up=clean(y)}
    @Synchronized fun look(x:Float,y:Float){if(x.isFinite()&&y.isFinite()){dx=(dx+x).coerceIn(-400f,400f);dy=(dy+y).coerceIn(-400f,400f)}}
    @Synchronized fun home(){reset=true}
    @Synchronized fun clear(){right=0f;forward=0f;up=0f;dx=0f;dy=0f}
    @Synchronized fun consume():Frame {val result=Frame(right,forward,up,dx,dy,reset);dx=0f;dy=0f;reset=false;return result}
}
