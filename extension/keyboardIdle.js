// SPDX-License-Identifier: GPL-2.0-only
// Serialized writes: activity during an in-flight dim is handled on the next tick.
export class KeyboardIdle {
    constructor({idle, brightness, ready, write, report}) {
        Object.assign(this, {idle, brightness, ready, write, report});
        this.saved = null;
        this.busy = false;
        this.stopped = false;
        this.failed = false;
        this.seconds = 0;
    }

    forget() { this.saved = null; }

    async tick(seconds) {
        if (this.stopped || this.busy || !this.ready()) return;
        const idle = this.idle();
        const expired = seconds > 0 && idle >= seconds * 1000;
        if (!expired || seconds !== this.seconds) this.failed = false;
        this.seconds = seconds;
        if (this.failed) return;
        if (!expired) {
            await this.restore();
            return;
        }
        const value = this.brightness();
        if (this.saved !== null || !Number.isInteger(value) || value <= 0 || value > 4) return;
        this.busy = true;
        this.saved = value; // Preserve the selected level even while the off write is pending.
        try {
            await this.write(0);
        } catch (error) {
            this.saved = null;
            this.failed = true; // Do not retry a failed write each second while idle.
            this.report(error);
        } finally {
            this.busy = false;
        }
        if (this.stopped) await this.restore();
    }

    async restore() {
        if (this.saved === null || this.busy) return;
        this.busy = true;
        try {
            await this.write(this.saved);
            this.saved = null;
        } catch (error) {
            this.report(error);
        } finally {
            this.busy = false;
        }
    }

    stop() {
        this.stopped = true;
        void this.restore();
    }
}
