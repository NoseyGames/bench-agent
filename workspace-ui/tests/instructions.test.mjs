import test from 'node:test';
import assert from 'node:assert/strict';
import {defaultPreferences,desktopInstructions} from '../preferences.js';
test('desktop prompt carries supported personalization and Git guidance',()=>{
  const p={...defaultPreferences(),name:'Avery',occupation:'Engineer',about:'Use accessible controls',responseStyle:'Concise',instructions:'Keep tests',memoryNotes:'Project uses C11',gitBranchPrefix:'feature/',gitCommitInstructions:'Describe the change',gitPrInstructions:'Include validation'};
  const text=desktopInstructions(p);
  for(const value of ['Avery','Engineer','Use accessible controls','Concise','Keep tests','Project uses C11','feature/','Describe the change','Include validation'])assert.ok(text.includes(value),value);
  p.customInstructions=false;p.memoryEnabled=false;const disabled=desktopInstructions(p);
  assert.ok(!disabled.includes('Keep tests'));assert.ok(!disabled.includes('Project uses C11'));
});
