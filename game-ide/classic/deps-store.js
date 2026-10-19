/* DEVWORLD IDE — approved-dependency selections (per sandbox runtime) and the
   tracked backend submission id used for revision comparison.

   Kept dependency-free and UMD-ish so it works both as a classic browser script
   (defines window.DWDeps) and in Node for unit tests
   (game-ide/tests/deps-store.test.js). */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  if (root) root.DWDeps = api;
})(typeof window !== 'undefined' ? window : null, function () {
  const DEPS_KEY = 'devworld.deps.v1';
  const SUBMISSION_KEY = 'devworld.submission.v1';

  let store = typeof localStorage !== 'undefined' ? localStorage : null;

  function read() {
    try {
      return JSON.parse((store && store.getItem(DEPS_KEY)) || '{}');
    } catch (e) {
      return {};
    }
  }

  return {
    /** Test seam: point the module at any {getItem,setItem} implementation. */
    _setStore(s) {
      store = s;
    },
    /** Approved dependency names selected for a runtime (a copy). */
    get(runtime) {
      const m = read();
      return Array.isArray(m[runtime]) ? m[runtime].slice() : [];
    },
    /** Select a deduped list of dependency names for a runtime. */
    set(runtime, list) {
      const m = read();
      m[runtime] = Array.from(new Set(list));
      try {
        if (store) store.setItem(DEPS_KEY, JSON.stringify(m));
      } catch (e) {}
      return m[runtime];
    },
    submission() {
      try {
        return (store && store.getItem(SUBMISSION_KEY)) || '';
      } catch (e) {
        return '';
      }
    },
    setSubmission(id) {
      try {
        if (store) store.setItem(SUBMISSION_KEY, id || '');
      } catch (e) {}
    },
  };
});
