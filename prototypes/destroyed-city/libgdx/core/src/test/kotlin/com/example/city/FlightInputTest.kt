package com.example.city
import org.junit.Assert.*
import org.junit.Test
class FlightInputTest {
    @Test fun lookConsumedOnceAndMovementHeld(){val input=FlightInput();input.move(1f,-1f);input.look(10f,20f);assertEquals(10f,input.consume().dx);val frame=input.consume();assertEquals(0f,frame.dx);assertEquals(1f,frame.right);assertEquals(-1f,frame.forward)}
    @Test fun pauseStopsAllAxes(){val input=FlightInput();input.move(1f,1f);input.lift(1f);input.look(5f,5f);input.clear();val f=input.consume();assertEquals(0f,f.right);assertEquals(0f,f.up);assertEquals(0f,f.dy)}
    @Test fun invalidAndExcessInputIsBounded(){val input=FlightInput();input.move(Float.NaN,30f);input.look(Float.NaN,0f);input.lift(-20f);val f=input.consume();assertEquals(0f,f.right);assertEquals(1f,f.forward);assertEquals(-1f,f.up);assertEquals(0f,f.dx)}
    @Test fun homeIsOneShot(){val input=FlightInput();input.home();assertTrue(input.consume().reset);assertFalse(input.consume().reset)}
}
