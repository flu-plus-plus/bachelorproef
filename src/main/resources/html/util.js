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

// Function called when you click a collapse button
function toggleCollapsed(){
    var $target = $(this).parents().eq(1);
    if($target.hasClass("collapsed")){
        $(this).text("─");
        $target.removeClass("collapsed");
    } else {
        $(this).text("+");
        $target.addClass("collapsed");
    }
}

// Function called when you click a close button
function closePanel(){
    var $target = $(this).parents().eq(1);
    $target.remove();
}

var $collapseToggle = $("<a>",{class:"panel-button toggle-collapse"}).text("─").on("click", toggleCollapsed);
var $closeButton = $("<a>",{class:"panel-button close-panel"}).append($("<span>",{class:"glyphicon glyphicon-remove"})).on("click", closePanel);

// Add a collapse button to all panels that don't have one yet
function refreshCollapsable(){
    $(".panel > .panel-head[hasCollapse!=true]").attr("hasCollapse", true).append($collapseToggle);
}

// Make a panel
function makePanel(x=30, y=30, title="", body=""){
    $panel = $('<div>',{class:"panel generic"});
    $panel.head = $('<div>',{class:"panel-head"}).text(title);
    $panel.body = $('<div>',{class:"panel-body"}).append(body);
    $panel.append($panel.head);
    $panel.append($panel.body);

    $panel.head.append($closeButton.clone(true));
    $panel.head.attr("hasCollapse", true).append($collapseToggle.clone(true));

    $panel.css({left:x,top:y,position:"absolute"});
    $panel.draggable({handle:".panel-head"});
    return $panel;
}