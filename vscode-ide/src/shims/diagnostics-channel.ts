// Browser stub for node:diagnostics_channel.
// @xterm/addon-ligatures (font loading diagnostics) imports { tracingChannel,
// channel } from it. In the browser there are no subscribers; a no-op stub is
// the correct behavior.
function makeChannel() {
  return {
    hasSubscribers: false,
    publish() {},
    subscribe() {},
    unsubscribe() {},
    bindStore() {},
    unbindStore() {}
  }
}

export function channel(_name?: string) {
  return makeChannel()
}

export function tracingChannel(_name?: string) {
  return makeChannel()
}

export function hasSubscribers(_name?: string) {
  return false
}

export default { channel, tracingChannel, hasSubscribers }
