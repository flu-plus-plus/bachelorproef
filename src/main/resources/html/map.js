/**
 * Script for handling maps.
 * Author: Sibert Aerts
 */

//------------//
// Class: Map //
//------------//

/// Make a new Map by the following attributes
/// imageRef: ref to the map image
/// ratio: horizontal/vertical ratio of the image
/// min/maxLat/Long: bounding box of the map image
var Map = function(name, imageRef, imageRatio, minLat, maxLat, minLong, maxLong, zoomable=false){
    this.name = name;
    this.imageRef = imageRef;
    this.imageRatio = imageRatio;
    this.zoomable = zoomable;
    this.box = {minLat: minLat, maxLat: maxLat, minLong: minLong, maxLong: maxLong};
}

/// Test if the given box fits inside the map.
Map.prototype.containsBox = function(box){
    if(this.box.minLat > box.minLat)
        return false;
    if(this.box.maxLat < box.maxLat)
        return false;
    if(this.box.minLong > box.minLong)
        return false;
    if(this.box.maxLong < box.maxLong)
        return false;
    return true;
}

// Returns a {width, height} object that matches the image's ratio that fits inside the given width and height
Map.prototype.fitTo = function(width, height){
    if(this.imageRatio > width/height)
        return {width: width, height: width / this.imageRatio};
    if(this.imageRatio < width/height)
        return {width: height * this.imageRatio, height: height};
    return {width: width, height: height};
}

// Find the various values necessary to properly crop the map to show the given focusBox
// width and height are the width and height of the viewport
// focusBox is the box of geocoordinates that we want to look at
// margin is a factor describing how much space should be between the box and edge of the viewport
Map.prototype.getCrop = function(width, height, focusBox, margin=1.2){
    var fullLat = this.box.maxLat - this.box.minLat;
    var fullLong = this.box.maxLong - this.box.minLong;

    var dLat = focusBox.maxLat - focusBox.minLat;
    var dLong = focusBox.maxLong - focusBox.minLong;

    var out = {};

    var viewLat = (dLat * margin) / fullLat;
    var viewLong = (dLong * margin) / fullLong;
    var viewBoxRatio = fullLat / fullLong * this.imageRatio * dLong / dLat;

    // Determine how our focus box can take up as much space as possible
    // without squashing or stretching the image it's projected on.
    if (viewBoxRatio > width / height){
        out.width = width / viewLong;
        out.height = out.width / this.imageRatio;
    } else {
        out.height = height / viewLat;
        out.width = out.height * this.imageRatio;
    }

    var pxPerLat = out.height / fullLat;
    var pxPerLong = out.width / fullLong;
    // Amount of lat/long margin within the viewport
    var marginLong = (width / pxPerLong - dLong) / 2;
    var marginLat = (height / pxPerLat - dLat) / 2;

    // out.box is the box of latitude/longitudes that's effectively shown in the viewport
    out.box = {};
    out.box.minLat = focusBox.minLat - marginLat;
    out.box.maxLat = focusBox.maxLat + marginLat;
    out.box.minLong = focusBox.minLong - marginLong;
    out.box.maxLong = focusBox.maxLong + marginLong;
    
    // The left/top offset the viewport has compared to the map
    out.left = (out.box.minLong - this.box.minLong) * pxPerLong;
    out.top = (this.box.maxLat - out.box.maxLat) * pxPerLat;
    return out;
}

// Format:
// Map.area = new Map(name, imageRef, imageRatio, minLat, maxLat, minLong, maxLong);
//     name:        What the map will be identified as, can be anything
//     imageRef:    A link to a file or web-image of a map. Has to be equirectangular projection, not just regular old Mercator.
//     imageRatio:  The image's ratio of width to height, since ideally you're working with an SVG where actual width/height doesn't matter.
//     min/maxLat/long: The box holding exactly all geopositions in the image.

Map.belgium = new Map("Belgium", "resource/belgium.svg", 1135.92/987.997, 49.2, 51.77, 2.19, 6.87);

// Map.earth = new Map("Earth", "resource/the_world.svg", 634.26/476.72, -114.5, 112.5, -170, 190);

// image credit: Wikipedia
Map.earth = new Map("Earth", "https://upload.wikimedia.org/wikipedia/commons/8/83/Equirectangular_projection_SW.jpg", 
    2058/1036, -92, 92, -181, 181);

// Add maps here so the visualizer can find them.
Map.maps = [Map.belgium, Map.earth];