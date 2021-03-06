const Tweener = imports.tweener.tweener;
const Mainloop = imports.mainloop;

function installFrameTicker() {
    // Set up Tweener to have a "frame pulse" from
    // our main rendering loop
    let ticker = {
        FRAME_RATE: 50,

        _init : function() {
        },

        start : function() {
            this._currentTime = 0;

            let me = this;
            this._timeoutID =
                Mainloop.timeout_add(Math.floor(1000 / me.FRAME_RATE),
                                     function() {
                                         me._currentTime += 1000 / me.FRAME_RATE;
                                         me.emit('prepare-frame');
                                         return true;
                                     });
        },

        stop : function() {
            if ('_timeoutID' in this) {
                Mainloop.source_remove(this._timeoutID);
                delete this._timeoutID;
            }

            this._currentTime = 0;
        },

        getTime : function() {
            return this._currentTime;
        }
    };
    imports.signals.addSignalMethods(ticker);

    Tweener.setFrameTicker(ticker);
}

function testSimpleTween() {
    var objectA = {
        x: 0,
        y: 0
    };

    var objectB = {
        x: 0,
        y: 0
    };

    Tweener.addTween(objectA, { x: 10, y: 10, time: 1, transition: "linear",
                                onComplete: function() { Mainloop.quit('testTweener');}});
    Tweener.addTween(objectB, { x: 10, y: 10, time: 1, delay: 0.5, transition: "linear" });

    Mainloop.run('testTweener');

    with (objectA) {
        assertEquals("A: x coordinate", 10, x);
        assertEquals("A: y coordinate", 10, y);
    }

    with (objectB) {
        assertEquals("B: x coordinate", 5, x);
        assertEquals("B: y coordinate", 5, y);
    }
}

function testOnFunctions() {
    var object = {
        start: false,
        update: false,
        complete: false
    };

    Tweener.addTween(object, { time: 0.1,
                               onStart: function () {
                                   object.start = true;
                               },
                               onUpdate: function () {
                                   object.update = true;
                               },
                               onComplete: function() {
                                   object.complete = true;
                                   Mainloop.quit('testOnFunctions');
                               }});

    Mainloop.run('testOnFunctions');

    with (object) {
        assertEquals("onStart was run", true, start);
        assertEquals("onUpdate was run", true, update);
        assertEquals("onComplete was run", true, complete);
    }
}

function testPause() {
    var objectA = {
        foo: 0
    };

    var objectB = {
        bar: 0
    };

    var objectC = {
        baaz: 0
    };

    Tweener.addTween(objectA, { foo: 100, time: 0.1 });
    Tweener.addTween(objectC, { baaz: 100, time: 0.1 });
    Tweener.addTween(objectB, { bar: 100, time: 0.1,
                               onComplete: function() { Mainloop.quit('testPause');}});
    Tweener.pauseTweens(objectA);
    assertEquals(false, Tweener.pauseTweens(objectB, "quux")); // This should do nothing

    /* Pause and resume should be equal to doing nothing */
    Tweener.pauseTweens(objectC, "baaz");
    Tweener.resumeTweens(objectC, "baaz");

    Mainloop.run('testPause');

    with (objectA) {
        assertEquals(0, foo);
    }

    with (objectB) {
        assertEquals(100, bar);
    }

    with (objectC) {
        assertEquals(100, baaz);
    }
}

function testRemoveTweens() {
    var object = {
        foo: 0,
        bar: 0,
        baaz: 0
    };

    Tweener.addTween(object, { foo: 50, time: 0.1,
                               onComplete: function() { Mainloop.quit('testRemoveTweens');}});
    Tweener.addTween(object, { bar: 50, time: 0.1 });
    Tweener.addTween(object, { baaz: 50, time: 0.1});

    /* The Tween on property foo should still be run after removing the other two */
    Tweener.removeTweens(object, "bar", "baaz");
    Mainloop.run('testRemoveTweens');

    with (object) {
        assertEquals(50, foo);
        assertEquals(0, bar);
        assertEquals(0, baaz);
    }
}

function testConcurrent() {
    var objectA = {
        foo: 0
    };

    var objectB = {
        bar: 0
    };

    /* The second Tween should override the first one, as
     * they act on the same object, property, and at the same
     * time.
     */
    Tweener.addTween(objectA, { foo: 100, time: 0.1 });
    Tweener.addTween(objectA, { foo: 0, time: 0.1 });

    /* In this case both tweens should be executed, as they don't
     * act on the object at the same time (the second one has a
     * delay equal to the running time of the first one) */
    Tweener.addTween(objectB, { bar: 100, time: 0.1 });
    Tweener.addTween(objectB, { bar: 150, time: 0.1, delay: 0.1,
                               onComplete: function () {
                                   Mainloop.quit('testConcurrent');}});

    Mainloop.run('testConcurrent');

    with (objectA) {
        assertEquals(0, foo);
    }

    with (objectB) {
        assertEquals(150, bar);
    }
}

function testPauseAllResumeAll() {
    var objectA = {
        foo: 0
    };
    var objectB = {
        bar: 0
    };

    Tweener.addTween(objectA, { foo: 100, time: 0.1 });
    Tweener.addTween(objectB, { bar: 100, time: 0.1,
                                onComplete: function () { Mainloop.quit('testPauseAllResumeAll');}});

    Tweener.pauseAllTweens();

    Mainloop.timeout_add(10, function () {
                             Tweener.resumeAllTweens();
                             return false;
                         });

    Mainloop.run('testPauseAllResumeAll');

    with (objectA) {
        assertEquals(100, foo);
    }

    with (objectB) {
        assertEquals(100, bar);
    }
}

function testRemoveAll() {
    var objectA = {
        foo: 0
    };
    var objectB = {
        bar: 0
    };

    Tweener.addTween(objectA, { foo: 100, time: 0.1 });
    Tweener.addTween(objectB, { bar: 100, time: 0.1 });

    Tweener.removeAllTweens();

    Mainloop.timeout_add(200,
                        function () {
                            assertEquals(0, objectA.foo);
                            assertEquals(0, objectB.bar);
                            Mainloop.quit('testRemoveAll');
                            return false;
                        });

    Mainloop.run('testRemoveAll');
}

function testImmediateTween() {
    var object = {
        foo: 100
    };

    Tweener.addTween(object, { foo: 50, time: 0, delay: 0 });
    Tweener.addTween(object, { foo: 200, time: 0.1,
                               onStart: function () {
                                   /* The immediate tween should set it to 50 before we run */
                                   assertEquals(50, object.foo);
                               },
                               onComplete: function () {
                                   Mainloop.quit('testImmediateTween');
                               }});

    Mainloop.run('testImmediateTween');

    with (object) {
        assertEquals(200, foo);
    }
}

function testAddCaller() {
    var object = {
        foo: 0
    };

    Tweener.addCaller(object, { onUpdate: function () {
                                    object.foo += 1;
                                },
                                onComplete: function () {
                                    Mainloop.quit('testAddCaller');
                                },
                                count: 10,
                                time: 0.1 });

    Mainloop.run('testAddCaller');

    with (object) {
        assertEquals(10, foo);
    }
}

function testGetTweenCount() {
    var object = {
        foo: 0,
        bar: 0,
        baaz: 0,
        quux: 0
    };

    assertEquals(0, Tweener.getTweenCount(object));

    Tweener.addTween(object, { foo: 100, time: 0.1 });
    assertEquals(1, Tweener.getTweenCount(object));
    Tweener.addTween(object, { bar: 100, time: 0.1 });
    assertEquals(2, Tweener.getTweenCount(object));
    Tweener.addTween(object, { baaz: 100, time: 0.1 });
    assertEquals(3, Tweener.getTweenCount(object));
    Tweener.addTween(object, { quux: 100, time: 0.1,
                               onComplete: function () {
                                   Mainloop.quit('testGetTweenCount');}});
    assertEquals(4, Tweener.getTweenCount(object));

    Tweener.removeTweens(object, "bar", "baaz");

    assertEquals(2, Tweener.getTweenCount(object));

    Mainloop.run('testGetTweenCount');

    assertEquals(0, Tweener.getTweenCount(object));
}

Tweener.registerSpecialProperty(
    'negative_x',
    function(obj) { return -obj.x; },
    function(obj, val) { obj.x = -val; }
);

function testSpecialProperty() {
    var objectA = {
        x: 0,
        y: 0
    };

    Tweener.addTween(objectA, { negative_x: 10, y: 10, time: 1,
                                transition: "linear",
                                onComplete: function() { Mainloop.quit('testSpecialProperty');}});

    Mainloop.run('testSpecialProperty');

    with (objectA) {
        assertEquals("A: x coordinate", -10, x);
        assertEquals("A: y coordinate", 10, y);
    }
}

Tweener.registerSpecialPropertyModifier('discrete',
                                        discrete_modifier,
                                        discrete_get);
function discrete_modifier(props) {
    return props.map(function (prop) { return { name: prop, parameters: null }; });
}
function discrete_get(begin, end, time, params) {
    return Math.floor(begin + time * (end - begin));
}

function testSpecialPropertyModifier() {
    var objectA = {
        x: 0,
        y: 0,
        xFraction: false,
        yFraction: false
    };

    Tweener.addTween(objectA, { x: 10, y: 10, time: 1,
                                discrete: ["x"],
                                transition: "linear",
                                onUpdate: function() {
                                    if (objectA.x != Math.floor(objectA.x))
                                        objectA.xFraction = true;
                                    if (objectA.y != Math.floor(objectA.y))
                                        objectA.yFraction = true;
                                },
                                onComplete: function() { Mainloop.quit('testSpecialPropertyModifier');}});

    Mainloop.run('testSpecialPropertyModifier');

    with (objectA) {
        assertEquals("A: x coordinate", 10, x);
        assertEquals("A: y coordinate", 10, y);
        assertEquals("A: x was fractional", false, xFraction);
        assertEquals("A: y was fractional", true, yFraction);
    }
}

Tweener.registerSpecialPropertySplitter(
    'xnegy',
    function(val) { return [ { name: "x", value: val },
                             { name: "y", value: -val } ]; }
);

function testSpecialPropertySplitter() {
    var objectA = {
        x: 0,
        y: 0
    };

    Tweener.addTween(objectA, { xnegy: 10, time: 1,
                                transition: "linear",
                                onComplete: function() { Mainloop.quit('testSpecialPropertySplitter');}});

    Mainloop.run('testSpecialPropertySplitter');

    with (objectA) {
        assertEquals("A: x coordinate", 10, x);
        assertEquals("A: y coordinate", -10, y);
    }
}

installFrameTicker();
gjstestRun();

