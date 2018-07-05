/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

const binary = require('node-pre-gyp');
const path = require('path');
const binding_path = binary.find(path.resolve(path.join(__dirname,'./package.json')));
const watchdog = require(binding_path);

let hasStarted = false;

exports.start = function(timeout) {
    if (hasStarted) {
        return;
    }
    hasStarted = true;
    watchdog.start(timeout);
    setInterval(function() {
        // let the C++ side know that the event loop is alive.
        watchdog.ping();
    }, 1000);
};

exports.exit = function(code) {
    watchdog.exit(code || 0);
}
