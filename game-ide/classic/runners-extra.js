/* DEVWORLD game IDE — extra in-browser runners: Ruby, PHP, R.
 *
 * Companion to ide.js, loaded by index.html BEFORE ide.js, which merges
 * window.EXTRA_RUNNERS into its RUNNERS registry.
 *
 * Each runner is print-based:
 *   window.EXTRA_RUNNERS = {
 *     ruby: async (code, helpers) => {...},
 *     php:  async (code, helpers) => {...},
 *     r:    async (code, helpers) => {...},
 *   }
 * where helpers = { print(text, cls), setStatus(text) }.
 *
 * Capture-based variants for a test runner:
 *   window.EXTRA_CAPTURE = {
 *     ruby: async (code, stdin) => '<stdout string>',
 *     php:  async (code, stdin) => '<stdout string>',
 *     r:    async (code, stdin) => '<stdout string>',
 *   }
 *
 * ERROR POLICY (documented): on user-code failure, EXTRA_RUNNERS prints the
 * error message via helpers.print(msg, 'err'). EXTRA_CAPTURE returns
 * whatever stdout/stderr was produced BEFORE the error, followed by the
 * error text (each part separated so a test can match on either).
 *
 * All three runtimes are lazy-loaded on first use (multi-MB WASM downloads)
 * with a status message shown while loading, then cached in module-level
 * variables so later runs are instant.
 */
(function () {
  'use strict';

  /* ---------------- pinned CDN URLs (verified 2026-10-19) ---------------- */
  var RUBY_UMD_URL  = 'https://cdn.jsdelivr.net/npm/@ruby/wasm-wasi@2.10.1/dist/browser.umd.js';
  var RUBY_WASM_URL = 'https://cdn.jsdelivr.net/npm/@ruby/3.4-wasm-wasi@2.10.1/dist/ruby+stdlib.wasm';
  var PHP_MJS_URL   = 'https://cdn.jsdelivr.net/npm/php-wasm@0.2.0/PhpWeb.mjs';
  var WEBR_MJS_URL  = 'https://webr.r-wasm.org/latest/webr.mjs';

  /* ---------------- module-level VM caches ---------------- */
  var rubyRT = null;  // { vm, wasi }
  var phpRT  = null;  // { php, outChunks, errChunks }
  var rRT    = null;  // WebR instance

  var textDec = new TextDecoder();
  var textEnc = new TextEncoder();

  function loadScriptTag(src) {
    return new Promise(function (resolve, reject) {
      var s = document.createElement('script');
      s.src = src;
      s.onload = function () { resolve(); };
      s.onerror = function () { reject(new Error('script load failed: ' + src)); };
      document.head.appendChild(s);
    });
  }

  function errText(e) {
    return String((e && e.message) || e);
  }

  function joinParts() {
    // Join stdout/stderr/error parts with single newlines, no doubles.
    var out = '';
    for (var i = 0; i < arguments.length; i++) {
      var p = arguments[i];
      if (!p) continue;
      if (out && out.charAt(out.length - 1) !== '\n') out += '\n';
      out += p;
    }
    return out;
  }

  function trimEnd(s) { return s.replace(/[\r\n]+$/, ''); }

  /* ============================ Ruby ============================ */
  /* @ruby/wasm-wasi UMD (bundled WASI shim) + ruby+stdlib.wasm binary.
   * Capture: DefaultRubyVM(mod, {consolePrint:false}) keeps WASI fd writes
   * in wasi.fds[1] (stdout) / wasi.fds[2] (stderr) File buffers; we reset
   * them before each run and decode afterwards.
   * Stdin: fd 0 is a plain in-memory File, so we seed its bytes and reset
   * its position before each run — gets/$stdin.read then work. */

  async function ensureRuby(helpers) {
    if (rubyRT) return rubyRT;
    helpers.setStatus('Loading Ruby runtime…');
    try {
      if (!window['ruby-wasm-wasi']) {
        // The UMD build AMD-detects (Monaco's loader defines window.define)
        // and would register as an AMD module instead of setting the global.
        // Stash `define` around the load, like gitpanel.js does for isomorphic-git.
        var hadDefine = Object.prototype.hasOwnProperty.call(window, 'define');
        var prevDefine = window.define;
        try { window.define = undefined; } catch (e) {}
        try {
          await loadScriptTag(RUBY_UMD_URL);
        } finally {
          if (hadDefine) { try { window.define = prevDefine; } catch (e) {} }
          else { try { delete window.define; } catch (e) {} }
        }
      }
      var DefaultRubyVM = window['ruby-wasm-wasi'].DefaultRubyVM;
      var mod = await WebAssembly.compileStreaming(fetch(RUBY_WASM_URL));
      var r = await DefaultRubyVM(mod, { consolePrint: false });
      // Ruby block-buffers stdout/stderr when they are not a tty (WASI fds
      // are plain memory files here), so without sync mode nothing would
      // reach the fd buffers until process exit. Enable sync once per VM.
      await r.vm.evalAsync('$stdout.sync = true; $stderr.sync = true');
      rubyRT = { vm: r.vm, wasi: r.wasi };
      return rubyRT;
    } finally {
      helpers.setStatus('');
    }
  }

  function rubyResetFds(rt, stdin) {
    var inBytes = textEnc.encode(stdin || '');
    var f0 = rt.wasi.fds[0];
    f0.file.data = new Uint8Array(inBytes);
    f0.file_pos = 0n;
    [1, 2].forEach(function (fd) {
      rt.wasi.fds[fd].file.data = new Uint8Array([]);
      rt.wasi.fds[fd].file_pos = 0n;
    });
  }

  function rubyStdout(rt) { return textDec.decode(rt.wasi.fds[1].file.data); }
  function rubyStderr(rt) { return textDec.decode(rt.wasi.fds[2].file.data); }

  async function rubyCapture(code, stdin) {
    var rt = await ensureRuby({ setStatus: function () {} });
    rubyResetFds(rt, stdin);
    var thrown = '';
    try {
      await rt.vm.evalAsync(code);
    } catch (e) {
      thrown = errText(e);
    }
    return joinParts(rubyStdout(rt), rubyStderr(rt), thrown);
  }

  async function runRuby(code, helpers) {
    try {
      await ensureRuby(helpers); // shows 'Loading Ruby runtime…' on first use
      var out = await rubyCapture(code, '');
      if (trimEnd(out)) helpers.print(trimEnd(out));
    } catch (e) {
      helpers.print(errText(e), 'err');
    }
  }

  /* ============================ PHP ============================ */
  /* seanmorris/php-wasm ESM build. new PhpWeb() defaults to PHP 8.4 and
   * pulls php8.4-web.mjs + its hashed .wasm from the same CDN dir.
   * Output arrives as 'output'/'error' events with e.detail = [textChunk]
   * (flushed per newline; remainder flushed by run's internal finally).
   * Stdin: php.buffers.stdin is a plain array drained by the default stdin
   * callback, so we push char codes before each run. NOTE: the STDIN
   * constant is not defined in this SAPI build — user code must read via
   * fopen('php://stdin','r') instead (verified working).
   * If user code has no <?php tag the wrapper treats it as inline HTML
   * (?> prefix), so plain `echo "hi";` still runs. */

  async function ensurePhp(helpers) {
    if (phpRT) return phpRT;
    helpers.setStatus('Loading PHP runtime…');
    try {
      var m = await import(PHP_MJS_URL);
      var php = new m.PhpWeb();
      var rt = { php: php, outChunks: [], errChunks: [] };
      php.addEventListener('output', function (e) { rt.outChunks.push(e.detail[0]); });
      php.addEventListener('error',  function (e) { rt.errChunks.push(e.detail[0]); });
      await new Promise(function (resolve, reject) {
        php.addEventListener('ready', function () { resolve(); }, { once: true });
        setTimeout(function () { reject(new Error('PHP runtime init timed out')); }, 120000);
      });
      phpRT = rt;
      return phpRT;
    } finally {
      helpers.setStatus('');
    }
  }

  async function phpCapture(code, stdin) {
    var rt = await ensurePhp({ setStatus: function () {} });
    rt.outChunks.length = 0;
    rt.errChunks.length = 0;
    if (stdin) {
      var codes = [];
      for (var i = 0; i < stdin.length; i++) codes.push(stdin.charCodeAt(i));
      rt.php.buffers.stdin.push.apply(rt.php.buffers.stdin, codes);
    }
    var thrown = '';
    try {
      await rt.php.run(code);
    } catch (e) {
      thrown = errText(e);
    }
    return joinParts(rt.outChunks.join(''), rt.errChunks.join(''), thrown);
  }

  async function runPhp(code, helpers) {
    try {
      await ensurePhp(helpers); // shows 'Loading PHP runtime…' on first use
      var out = await phpCapture(code, '');
      if (trimEnd(out)) helpers.print(trimEnd(out));
    } catch (e) {
      helpers.print(errText(e), 'err');
    }
  }

  /* ============================ R (webR) ============================ */
  /* webR from r-wasm.org. Printed output is captured by wrapping user code
   * in capture.output({...}) and collapsing to one string; the R result is
   * a character vector, converted with .toJs() -> { values: [...] }.
   * Stdin is not wired (webR eval has no stdin channel) — ignored.
   * Errors: evalR rejects; the message is returned as the error part. */

  async function ensureR(helpers) {
    if (rRT) return rRT;
    helpers.setStatus('Loading R runtime…');
    try {
      var m = await import(WEBR_MJS_URL);
      // NOTE: this webR build bakes in baseUrl "https://webr.r-wasm.org/main/"
      // instead of deriving it from the loader URL, so pass it explicitly —
      // assets (webr-worker.js, R.wasm, R.js) then load from the same dir as
      // the loader itself.
      var base = WEBR_MJS_URL.slice(0, WEBR_MJS_URL.lastIndexOf('/') + 1);
      var webr = new m.WebR({ baseUrl: base });
      await webr.init();
      rRT = webr;
      return rRT;
    } finally {
      helpers.setStatus('');
    }
  }

  async function rCapture(code /*, stdin ignored */) {
    var webr = await ensureR({ setStatus: function () {} });
    var wrapped = 'paste(capture.output({\n' + code + '\n}), collapse="\n")';
    var thrown = '';
    var text = '';
    try {
      var shelter = await webr.evalR(wrapped);
      var js = await shelter.toJs();
      var vals = (js && js.values) || [];
      text = vals.join('\n');
    } catch (e) {
      thrown = errText(e);
    }
    return joinParts(text, thrown);
  }

  async function runR(code, helpers) {
    try {
      await ensureR(helpers); // shows 'Loading R runtime…' on first use
      var out = await rCapture(code, '');
      if (trimEnd(out)) helpers.print(trimEnd(out));
    } catch (e) {
      helpers.print(errText(e), 'err');
    }
  }

  /* ---------------- public surface ---------------- */
  window.EXTRA_RUNNERS = {
    ruby: runRuby,
    php: runPhp,
    r: runR,
  };

  window.EXTRA_CAPTURE = {
    ruby: function (code, stdin) { return rubyCapture(code, stdin || ''); },
    php:  function (code, stdin) { return phpCapture(code, stdin || ''); },
    r:    function (code, stdin) { return rCapture(code, stdin || ''); },
  };
})();
