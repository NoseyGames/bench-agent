/* Severity is a conservative UI explanation, never an allowlist for shell code.
 * Every command and file replacement needs a fresh, action-bound decision. */
export function assessAction(event){
  if(event.type==='desktop')return {severity:['screenshot','observe'].includes(event.name)?'high':'critical',reason:event.name==='observe'?'Shares the selected app’s accessible window and control text with the selected model during this run. Password values are omitted. Screenshots require separate approval.':event.name==='screenshot'?'Captures the selected app and sends its visible contents to the selected model. Close private content first.':'Real desktop input may submit data, delete content, or change settings. This approval allows one action only, followed by a structured state check.',requiresApproval:true};
  if(event.type==='command'){
    const destructive=/\b(remove-item|del|erase|rmdir|rd|rm|format|diskpart|shutdown|reg|bcdedit|netsh|set-mppreference|invoke-webrequest|curl|wget|start-process)\b|\b(git\s+(reset|clean))\b/i.test(event.text);
    return {severity:destructive?'critical':'high',reason:destructive?'This command may delete data, change system settings, transfer data, or launch another program.':'Shell commands can execute arbitrary code and affect files or apps anywhere your account can access. Their effects cannot be reliably predicted.',requiresApproval:true};
  }
  if(event.type==='preview')return {severity:event.before?'high':'medium',reason:event.before?'Replaces an existing file. Review the complete replacement before allowing it.':'Creates a file. Its contents may affect later programs or commands.',requiresApproval:true};
  return {severity:'low',reason:'Status or result only.',requiresApproval:false};
}
