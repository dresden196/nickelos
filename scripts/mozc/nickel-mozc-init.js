// NickelOS. BSD-3-Clause, as Mozc.

/**
 * @fileoverview Starts ChromeOS's Japanese input method on Mozc in
 * WebAssembly. The extension's engine was a Native Client module
 * (nacl_session_handler.nmf), which Chrome no longer runs, so Japanese typed
 * nothing. nacl_mozc.js (Mozc's own, from its last release with the NaCl
 * extension) talks to an element by postMessage and 'message' events; this
 * gives it one, backed by nickel-mozc.wasm (mozc_wasm.cc), answering as the
 * module did: {id, cmd} for a command, {id, event} for an event, and the
 * InitializeDone event first. The user's history and dictionary live in
 * /mozc, kept in IndexedDB (synced when the extension asks to SyncToFile).
 * Takes the place of nacl_mozc_init.js.
 */

'use strict';

document.addEventListener('readystatechange', function() {
  if (document.readyState != 'complete') {
    return;
  }
  if (!chrome || !chrome.input || !chrome.input.ime) {
    console.error('chrome.input.ime APIs are not available');
    return;
  }
  var module = document.createElement('div');
  module.id = 'nacl_mozc_object';
  var naclMozc = new mozc.NaclMozc(/** @type {!HTMLEmbedElement} */ (module));
  // As nacl_mozc_init.js: the option page reaches it through
  // chrome.extension.getBackgroundPage()['newOptionPage'].
  window['newOptionPage'] = naclMozc.newOptionPage.bind(naclMozc);
  document.body.appendChild(module);
  nickelStartMozc(module).catch(function(error) {
    console.error('nickel-mozc: the engine did not start', error);
  });
}, true);

async function nickelStartMozc(module) {
  var PROFILE = '/mozc';
  var engine = await createNickelMozc();
  engine.FS.mkdir(PROFILE);
  engine.FS.mount(engine.FS.filesystems.IDBFS, {}, PROFILE);
  await new Promise(function(resolve) {
    engine.FS.syncfs(true, resolve);
  });
  var init = engine.cwrap('mozc_init', 'string', ['string']);
  var evalCommand = engine.cwrap('mozc_eval', 'string', ['string']);
  var versions = engine.cwrap('mozc_versions', 'string', []);
  var isValidReading =
      engine.cwrap('mozc_is_valid_reading', 'number', ['string']);
  var posList = engine.cwrap('mozc_pos_list', 'string', []);

  var reply = function(message) {
    module.dispatchEvent(
        new MessageEvent('message', {data: JSON.stringify(message)}));
  };
  // Answers in order and after the caller returns, as the module did.
  var queue = Promise.resolve();
  module['postMessage'] = function(text) {
    queue = queue.then(async function() {
      var message = JSON.parse(text);
      if (message.cmd) {
        var command = JSON.parse(evalCommand(JSON.stringify(message.cmd)));
        // Mozc renamed Output.candidates, which nacl_mozc.js (2018) reads,
        // to candidate_window; the fields inside are the same.
        if (command.output && command.output.candidate_window) {
          command.output.candidates = command.output.candidate_window;
          delete command.output.candidate_window;
        }
        reply({id: message.id, cmd: command});
        return;
      }
      var event = message.event || {};
      var response = {type: event.type};
      if (event.type == 'SyncToFile') {
        response.result = await new Promise(function(resolve) {
          engine.FS.syncfs(false, function(error) {
            resolve(!error);
          });
        });
      } else if (event.type == 'GetVersionInfo') {
        Object.assign(response, JSON.parse(versions()));
      } else if (event.type == 'GetPosList') {
        response.posList = JSON.parse(posList());
      } else if (event.type == 'IsValidReading') {
        response.data = event.data;
        response.result = !!event.data && isValidReading(event.data) == 1;
      } else {
        response.error = 'Unsupported event';
      }
      reply({id: message.id, event: response});
    }).catch(function(error) {
      console.error('nickel-mozc:', error);
    });
  };
  module.dispatchEvent(new Event('load'));
  reply(JSON.parse(init(PROFILE)));
}
