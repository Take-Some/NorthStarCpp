    // Pure deterministic viewmodel recoil. Sampling is frame-rate independent:
    // the shot impulse is authored once; no random camera noise and no
    // modification of the physical muzzle or the authoritative aim ray.
    class WeaponRecoilMotion {
        #time=1;
        #back=0;
        #arm=0;
        #snap=.042;
        #accent=.038;
        #recovery=.135;
        #aftershock=.20;

        trigger({weaponBackMeters=0,armImpulse=0,
                 snapSeconds=.045,accentSeconds=.035,
                 settleSeconds=.14,aftershock=.18}={}) {
            const values=[weaponBackMeters,armImpulse,snapSeconds,
                          accentSeconds,settleSeconds,aftershock];
            if(!values.every(Number.isFinite) ||
               weaponBackMeters<0 || weaponBackMeters>.06 ||
               armImpulse<0 || armImpulse>1 ||
               snapSeconds<.015 || snapSeconds>.2 ||
               accentSeconds<.01 || accentSeconds>.2 ||
               settleSeconds<.06 || settleSeconds>.8 ||
               aftershock<0 || aftershock>1)
                throw new RangeError("Invalid authored recoil motion");
            // Closely spaced shots compound but cannot push the gun through
            // the camera or accumulate unlimited elbow bending.
            const existing=this.sample();
            this.#back=Math.min(.048,weaponBackMeters+existing.backMeters*.35);
            this.#arm=Math.min(1,armImpulse+existing.armImpulse*.30);
            this.#snap=snapSeconds;
            this.#accent=accentSeconds;
            this.#recovery=settleSeconds;
            this.#aftershock=aftershock;
            this.#time=0;
        }

        advance(dt) {
            if(!Number.isFinite(dt) || dt<0 || dt>.5)
                throw new RangeError("Invalid weapon recoil frame time");
            this.#time=Math.min(this.#time+dt,
                this.#snap+this.#accent+this.#recovery+.3);
            return this.sample();
        }

        sample() {
            const t=this.#time;
            const snap=this.#snap,accent=this.#accent;
            const finish=snap+accent+this.#recovery;
            if(t>=finish)return {backMeters:0,armImpulse:0,active:false};
            let shape;
            if(t<snap) {
                const u=t/snap;
                shape=1-(1-u)*(1-u)*(1-u);
            } else if(t<snap+accent) {
                const u=(t-snap)/accent;
                shape=1-.16*u;
            } else {
                const u=(t-snap-accent)/this.#recovery;
                // A critically settled recoil with one small, authored
                // aftershock, not continuous random jitter.
                const spring=Math.pow(1-u,2)*(1+.18*Math.sin(2*Math.PI*u));
                shape=.84*Math.max(0,spring);
            }
            const armShape=shape*(1+this.#aftershock*.10*
                Math.sin(Math.min(1,t/finish)*Math.PI*3));
            return {
                backMeters:this.#back*shape,
                armImpulse:Math.min(1,Math.max(0,this.#arm*armShape)),
                active:true
            };
        }

        reset() {this.#time=1;this.#back=0;this.#arm=0;}
    }
