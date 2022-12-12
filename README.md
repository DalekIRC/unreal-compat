# <div align="center">DalekIRC's UnrealIRCd Module <img src="https://cdn-icons-png.flaticon.com/512/5229/5229377.png" height="45" width="45"></div>

<div align="center">
This module provides compatibility and extended functionality with DalekIRC Services.
  
### <b>Requires UnrealIRCd 6 or later</b>.
</div>


## <div align="center">Background</div>
<div align="center">
We're doing away with services bots, and moving their commands server-side!

So you'll just do `/COMMAND` instead of any `/ns COMMAND` or indeed `/msg nickserv COMMAND`.
</div>

## <div align="center">Advantages</div>
- Configure who can do what using the UnrealIRCd configuration. This keeps configuration all in one place.
- Use UnrealIRCd operclass blocks to fully customise who can do what. For example, you can choose which oper can use the `/SUSPEND` command by specifying it in the [Operclass block](https://www.unrealircd.org/docs/Operclass_block). See below for how to set up your operblock.
- This keeps configuration all in one place
- A more native feel for clients, for example, proper numerics regarding reasons `/CREGISTER` might have failed (Example, `ERR_CHANOPRIVSNEEDED`)
- Sideswipes security issues pertaining to the many clients and scripts out there who assume it's okay to message a bot with sensitive information. An example of this is the fact that some networks do not use NickServ as their authentication mechanism, and instead could use something called "X" or something completely different.

This compliments other things that have moved to server commands for security reasons, and is, for example, the reason why [SASL](https://ircv3.net/specs/extensions/sasl-3.2) and [Account Registration](https://ircv3.net/specs/extensions/account-registration) (both server-side commands) exists in IRC.

## <div align="center">Unreal Commands Implemented By DalekIRC</div>

| Command | Name | Description |
|---|---|---|
| AJOIN | Auto-Join | This command lets you view and manage your auto-join list. |
| CREGISTER | Channel Register | This command lets you register a channel to your account. |
| MAIL | Mail | This command lets you send messages to users who are offline. They will see them as normal messages when they come back online. |
| SUSPEND | Suspend | This command lets privileged opers with operclass type `services { can_suspend; }` prevent a specific account from being used. |
| UNSUSPEND | Unsuspend | This command lets privileged opers with operclass type `services { can_unsuspend; }` unsuspend accounts. |
| CERTFP | Certificate Fingerprint | This command lets you view and manage your authentication fingerprints used in SASL |

## <div align="center">OperClass Permissions</div>
In order to use certain commands, you'll need to give some permissions in your [Operclass block](https://www.unrealircd.org/docs/Operclass_block).

This list is updated as and when things are added and required, but, as per the Work-In-Progress sign at the top, please expect changes. If you think this list looks incomplete, that will be why =]

```
operclass example-services-boss {
  permissions {
    // here is the important part
    services {
      can_suspend;
      can_unsuspend;
    }
  }
  ```
