package com.example.city

import com.badlogic.gdx.ApplicationAdapter
import com.badlogic.gdx.Gdx
import com.badlogic.gdx.graphics.GL20
import com.badlogic.gdx.graphics.PerspectiveCamera
import com.badlogic.gdx.graphics.g3d.ModelBatch
import com.badlogic.gdx.math.Vector3
import kotlin.math.*

class CityPreview(val input:FlightInput,private val ready:()->Unit):ApplicationAdapter(){
    private lateinit var city:DestroyedCity
    private lateinit var batch:ModelBatch
    private lateinit var camera:PerspectiveCamera
    private var generated=false
    private var frames=0
    private var yaw=0f
    private var pitch=18f
    private val citySize=12
    override fun create(){
        city=DestroyedCity();batch=ModelBatch()
        camera=PerspectiveCamera(67f,Gdx.graphics.width.toFloat(),Gdx.graphics.height.toFloat())
        camera.near=.15f;camera.far=1800f;home()
    }
    private fun home(){camera.position.set((citySize-1)*21f,65f,-45f);yaw=0f;pitch=18f}
    override fun resize(width:Int,height:Int){camera.viewportWidth=width.toFloat();camera.viewportHeight=height.toFloat();camera.update()}
    override fun render(){
        Gdx.gl.glViewport(0,0,Gdx.graphics.width,Gdx.graphics.height)
        Gdx.gl.glClearColor(.50f,.57f,.63f,1f)
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT or GL20.GL_DEPTH_BUFFER_BIT)
        if(!generated){
            // Present a frame before the original synchronous GL-model generator.
            if(frames++==0)return
            city.generate(citySize=citySize,blockSize=42f,roadWidth=10f,destruction=.5f)
            generated=true;input.clear();ready()
        }
        val controls=input.consume();if(controls.reset)home()
        yaw+=controls.dx*.16f;pitch=(pitch+controls.dy*.16f).coerceIn(-89f,89f)
        val angle=yaw*PI.toFloat()/180;val tilt=pitch*PI.toFloat()/180
        val direction=Vector3(sin(angle)*cos(tilt),-sin(tilt),cos(angle)*cos(tilt))
        camera.direction.set(direction);camera.up.set(Vector3.Y)
        val movement=Vector3(cos(angle)*controls.right+sin(angle)*controls.forward,controls.up,-sin(angle)*controls.right+cos(angle)*controls.forward)
        if(movement.len2()>1)movement.nor()
        camera.position.mulAdd(movement,30f*Gdx.graphics.deltaTime.coerceIn(0f,.05f));camera.update()
        // Original renderer and original generated ModelInstances, no substitutions.
        city.render(batch,camera)
    }
    override fun pause(){input.clear()}
    override fun dispose(){city.clear();batch.dispose()}
}
