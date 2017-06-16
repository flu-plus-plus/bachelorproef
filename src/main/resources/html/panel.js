
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

var basePanelX = 30;
var panelX = basePanelX + 30;
var basePanelY = 460;
var panelY = basePanelY + 30;

/// Make a new panel for the specified town, tied to the given visualizer.
var TownPanel = function(visualizer, townId){
    this.visualizer = visualizer;
    this.id = townId;

    var town = visualizer.towns[townId];

    // Get the x and y coordinates for the new element
    var x = panelX;
    var y = panelY;
    panelX = (panelX - basePanelX + 30) % 330 + basePanelX;
    panelY = (panelY - basePanelY + 30) % 240 + basePanelY;

    var text = 
    `<div> Infected: <span class=\"info infected\">-</span> /
    <span class=\"info \">${town.size}</span> =
    <span class=\"info percent\">-</span> </div>
    <div> Most infected: <span class=\"info\">${town.max.infected}</span> on day
    <span class=\"info clickable\" onclick="visualizer.updateDay(${town.max.day})">${town.max.day}</span> </div>`;

    var $panel = this.$element = makePanel(x, y, town.name, text);
    $panel.css("min-width", "200px");
    $panel.body.attr("town", noSpace(town.name));

    $("body").append($panel);

    this.$graph = $("<div>", {class: "town-graph"});
    $panel.body.append(this.$graph);
    this.makeGraph();
}

TownPanel.prototype.makeGraph = function(){
    var $target = this.$graph;
    var town = this.visualizer.towns[this.id];

    // Clear out the previous graph.
    $target.svg("destroy");
    $target.html("");

    $target.svg({settings: {width: "220px", height:"120px"}});
    var $svg = $target.svg("get");

    var percentList = [];
    for(var i in this.visualizer.days)
        percentList.push((this.visualizer.days[i][this.id]||0) * 100 / town.size);

    var percentLabels = [];
    for(var i=0;i<=100;i+=10) percentLabels.push(i + "%");

    $svg.graph.noDraw();

    $svg.graph.addSeries("Infected", percentList, "#186", "#2b8", 1);
    $svg.graph.options({barGap:0});
    $svg.graph.legend.show(false);
    $svg.graph.area(0.18, 0.1, 0.98, 0.85);
    $svg.graph.gridlines({stroke: '#aaf', strokeDashArray: '2,4'});
    $svg.graph.yAxis.ticks(10, 5, 8).labels(percentLabels).scale(0, clamp(town.max.infected / town.size * 120, 10, 100));
    $svg.graph.xAxis.ticks(50, 30, 1).labels("", "transparent");

    $svg.graph.redraw();

    // Day indicator
    var $wrap = $("<div>",{class:"indicator-wrapper"});
    var $indicator = $("<div>",{class:"indicator"});
    $wrap.append($indicator);
    $target.append($wrap);

    this.visualizer.updateGraph();
}

TownPanel.prototype.remove = function(){
    this.$element.remove();
}

TownPanel.prototype.toTop = function(){
    $('body').append(this.$element);
}