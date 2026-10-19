/**
 * In-memory per-API-key token bucket for abuse control.
 * Each bucket holds `capacity` tokens and refills at `refillPerHour`.
 */

export interface QuotaDecision {
  allowed: boolean;
  retryAfterSec: number;
}

export class TokenBucket {
  private buckets = new Map<string, { tokens: number; updatedAt: number }>();

  constructor(
    private readonly capacity: number,
    private readonly refillPerHour: number,
  ) {
    if (capacity <= 0 || refillPerHour <= 0) {
      throw new Error("TokenBucket requires positive capacity and refill rate");
    }
  }

  take(key: string): QuotaDecision {
    const now = Date.now();
    let bucket = this.buckets.get(key);
    if (!bucket) {
      bucket = { tokens: this.capacity, updatedAt: now };
      this.buckets.set(key, bucket);
    }
    const elapsedHours = (now - bucket.updatedAt) / 3_600_000;
    bucket.tokens = Math.min(this.capacity, bucket.tokens + elapsedHours * this.refillPerHour);
    bucket.updatedAt = now;

    if (bucket.tokens >= 1) {
      bucket.tokens -= 1;
      return { allowed: true, retryAfterSec: 0 };
    }
    const retryAfterSec = Math.max(
      1,
      Math.ceil(((1 - bucket.tokens) / this.refillPerHour) * 3600),
    );
    return { allowed: false, retryAfterSec };
  }

  /** For tests: how many whole tokens are currently available. */
  peek(key: string): number {
    const bucket = this.buckets.get(key);
    return bucket ? Math.floor(bucket.tokens) : this.capacity;
  }
}
