/** Процедурная звуковая атмосфера: ветер, далёкие взрывы и перестрелка */
export class Ambience {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private timers: number[] = [];
  private muted = false;
  private noise: AudioBuffer | null = null;

  start() {
    if (this.ctx) {
      void this.ctx.resume();
      return;
    }
    try {
      const AC = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      this.ctx = new AC();
    } catch {
      return;
    }
    const ctx = this.ctx;
    this.master = ctx.createGain();
    this.master.gain.value = this.muted ? 0 : 0.8;
    this.master.connect(ctx.destination);

    // шум (коричневый)
    const len = ctx.sampleRate * 4;
    const buf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = buf.getChannelData(0);
    let last = 0;
    for (let i = 0; i < len; i++) {
      const w = Math.random() * 2 - 1;
      last = (last + 0.02 * w) / 1.02;
      d[i] = last * 3.5;
    }
    this.noise = buf;

    // ветер
    const wind = ctx.createBufferSource();
    wind.buffer = buf;
    wind.loop = true;
    const lp = ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = 420;
    const wg = ctx.createGain();
    wg.gain.value = 0.35;
    const lfo = ctx.createOscillator();
    lfo.frequency.value = 0.12;
    const lfoGain = ctx.createGain();
    lfoGain.gain.value = 0.18;
    lfo.connect(lfoGain);
    lfoGain.connect(wg.gain);
    wind.connect(lp);
    lp.connect(wg);
    wg.connect(this.master);
    wind.start();
    lfo.start();

    const loop = (fn: () => void, min: number, max: number) => {
      const t = window.setTimeout(() => {
        fn();
        loop(fn, min, max);
      }, (min + Math.random() * (max - min)) * 1000);
      this.timers.push(t);
    };
    loop(() => this.boom(), 6, 16);
    loop(() => this.gunfire(), 9, 22);
  }

  private boom() {
    if (!this.ctx || !this.master || !this.noise) return;
    const ctx = this.ctx;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    const f = ctx.createBiquadFilter();
    f.type = 'lowpass';
    f.frequency.value = 160 + Math.random() * 120;
    const g = ctx.createGain();
    const t = ctx.currentTime + Math.random() * 0.2;
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(1.3, t + 0.05);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 2.8);
    src.connect(f);
    f.connect(g);
    g.connect(this.master);
    src.start(t, Math.random() * 2, 3);
  }

  private gunfire() {
    if (!this.ctx || !this.master || !this.noise) return;
    const ctx = this.ctx;
    const bursts = 4 + Math.floor(Math.random() * 10);
    const pan = ctx.createStereoPanner();
    pan.pan.value = Math.random() * 2 - 1;
    pan.connect(this.master);
    for (let i = 0; i < bursts; i++) {
      const t = ctx.currentTime + i * (0.07 + Math.random() * 0.03);
      const src = ctx.createBufferSource();
      src.buffer = this.noise;
      const f = ctx.createBiquadFilter();
      f.type = 'bandpass';
      f.frequency.value = 900 + Math.random() * 500;
      const g = ctx.createGain();
      g.gain.setValueAtTime(0.18, t);
      g.gain.exponentialRampToValueAtTime(0.0001, t + 0.12);
      src.connect(f);
      f.connect(g);
      g.connect(pan);
      src.start(t, Math.random() * 3, 0.15);
    }
  }

  setMuted(m: boolean) {
    this.muted = m;
    if (this.master && this.ctx) this.master.gain.setTargetAtTime(m ? 0 : 0.8, this.ctx.currentTime, 0.1);
  }

  dispose() {
    this.timers.forEach((t) => clearTimeout(t));
    this.timers = [];
    void this.ctx?.close();
    this.ctx = null;
  }
}
