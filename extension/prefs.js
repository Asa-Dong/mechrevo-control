// SPDX-License-Identifier: GPL-2.0-only
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';
import {CurveEditor} from './curveEditor.js';
export default class Preferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        this.editor = new CurveEditor(window);
    }
}
