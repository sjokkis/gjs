function testImporter() {

    assertNotUndefined(imports);

    assertRaises(function() { const m = imports.nonexistentModuleName; });
    assertRaises(function() { const m = imports.alwaysThrows; });

    // Import a non-broken module
    const foobar = imports.foobar;
    assertNotUndefined(foobar);
    assertNotUndefined(foobar.foo);
    assertNotUndefined(foobar.bar);
    assertEquals(foobar.foo, "This is foo");
    assertEquals(foobar.bar, "This is bar");

    // check that importing a second time gets the same object
    foobar.somethingElse = "Should remain";
    const foobar2 = imports.foobar;
    assertNotUndefined(foobar2.somethingElse);
    assertEquals(foobar2.somethingElse, "Should remain");

    // Try a sub-module
    const subB = imports.subA.subB;
    assertNotUndefined(subB);
    const subFoobar = subB.foobar;
    assertNotUndefined(subFoobar);
    assertNotUndefined(subFoobar.foo);
    assertNotUndefined(subFoobar.bar);
    assertEquals(subFoobar.foo, "This is foo");
    assertEquals(subFoobar.bar, "This is bar");
    // subFoobar should not be the same as foobar, even though
    // they have the same basename.
    assertUndefined(subFoobar.somethingElse);
    // importing subFoobar a second time should get the same module
    subFoobar.someProp = "Should be here";
    const subFoobar2 = imports.subA.subB.foobar;
    assertNotUndefined(subFoobar2.someProp);
    assertEquals(subFoobar2.someProp, "Should be here");
}

function testImporterMetaProps() {
    const subA = imports.subA;
    const subB = imports.subA.subB;

    assertNull('imports module name', imports.__moduleName__);
    assertNull('imports has no parent', imports.__parentModule__);
    assertEquals('imports.subA module name', 'subA', subA.__moduleName__);
    assertEquals('imports.subA parent module', imports, subA.__parentModule__);
    assertEquals('imports.subA.subB module name', 'subB', subB.__moduleName__);
    assertEquals('imports.subA.subB parent module', subA, subB.__parentModule__);
}

function testImporterEnumerate() {
    const subA = imports.subA;
    const subB = imports.subA.subB;
    let subModules = [];

    for (let module in subA) {
        subModules.push(module);
    }

    assertNotEquals(subModules.indexOf('subB'), -1);
    assertEquals(subModules.indexOf('foo'), -1);

    for (let module in subB) {
        subModules.push(module);
    }

    assertNotEquals(subModules.indexOf('baz'), -1);
    assertNotEquals(subModules.indexOf('foobar'), -1);

}

gjstestRun();