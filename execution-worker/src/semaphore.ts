/**
 * Counting semaphore with FIFO fairness and acquisition timeout.
 * Bounds how many containers may run concurrently.
 */

export class Semaphore {
  private permits: number;
  private waiters: Array<() => void> = [];

  constructor(permits: number) {
    if (!Number.isInteger(permits) || permits <= 0) {
      throw new Error("Semaphore requires a positive integer permit count");
    }
    this.permits = permits;
  }

  async acquire(timeoutMs: number): Promise<boolean> {
    if (this.permits > 0) {
      this.permits -= 1;
      return true;
    }
    return new Promise<boolean>((resolve) => {
      const waiter = () => {
        clearTimeout(timer);
        this.permits -= 1;
        resolve(true);
      };
      const timer = setTimeout(() => {
        const idx = this.waiters.indexOf(waiter);
        if (idx !== -1) this.waiters.splice(idx, 1);
        resolve(false);
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  release(): void {
    const next = this.waiters.shift();
    if (next) {
      next();
    } else {
      this.permits += 1;
    }
  }

  /** For tests: permits currently free (not counting queued waiters). */
  available(): number {
    return this.permits;
  }
}
