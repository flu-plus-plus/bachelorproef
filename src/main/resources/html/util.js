/**
 * Script for useful little methods.
 * Author: Sibert Aerts
 */

//------//
// Etc. //
//------//

// Remove all spaces from a given string.
var noSpace = s => s.split(" ").join('');

// Nicely format a given value p as a percentage showing d digits after the period.
var percentFormat = (p, d=1) => (100*p).toFixed(d)+'%';

// Clamp val to [l, r]
var clamp = (val, l, r) => val > r ? r : val < l ? l : val;

// Refresh Bootstrap's tooltips
var refreshTooltips = () => $('[data-toggle="tooltip"]').tooltip(); 

// Refresh jQuery-UI's draggable panels
var refreshDraggable = () => $(".panel").draggable({handle:".panel-head"});

// Await a certain length of time.
var sleep = ms => new Promise(resolve => setTimeout(resolve, ms));