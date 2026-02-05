/**
 * node-libzsh - Node.js native module exposing libzsh parser and ZLE
 */

'use strict';

// Load the native addon
const binding = require('../build/Release/node_libzsh.node');

// Export all functions from the native binding
module.exports = binding;
